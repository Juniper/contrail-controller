#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains config data model for schema transformer
"""

import sys
reload(sys)
sys.setdefaultencoding('UTF8')

import copy
import uuid

import re
import cfgm_common as common

from cfgm_common.exceptions import *
from cfgm_common import svc_info
from cfgm_common.vnc_db import DBBase
from vnc_api.vnc_api import *

from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from cfgm_common.uve.virtual_network.ttypes import *
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
try:
    # python2.7
    from collections import OrderedDict
except:
    # python2.6
    from ordereddict import OrderedDict
import jsonpickle

_PROTO_STR_TO_NUM = {
    'icmp': '1',
    'tcp': '6',
    'udp': '17',
    'any': 'any',
}

SGID_MIN_ALLOC = common.SGID_MIN_ALLOC


def _access_control_list_update(acl_obj, name, obj, entries):
    if acl_obj is None:
        if entries is None:
            return None
        acl_obj = AccessControlList(name, obj, entries)
        try:
            DBBaseST._vnc_lib.access_control_list_create(acl_obj)
            return acl_obj
        except (NoIdError, BadRequest) as e:
            DBBaseST._logger.error(
                "Error while creating acl %s for %s: %s",
                name, obj.get_fq_name_str(), str(e))
        return None
    else:
        if entries is None:
            try:
                DBBaseST._vnc_lib.access_control_list_delete(id=acl_obj.uuid)
            except NoIdError:
                pass
            return None

        # if entries did not change, just return the object
        if acl_obj.get_access_control_list_entries() == entries:
            return acl_obj

        # Set new value of entries on the ACL
        acl_obj.set_access_control_list_entries(entries)
        try:
            DBBaseST._vnc_lib.access_control_list_update(acl_obj)
        except HttpError as he:
            DBBaseST._logger.error(
                "HTTP error while updating acl %s for %s: %d, %s",
                name, obj.get_fq_name_str(), he.status_code, he.content)
        except NoIdError:
            DBBaseST._logger.error("NoIdError while updating acl %s for %s",
                                   name, obj.get_fq_name_str())
    return acl_obj
# end _access_control_list_update


class DBBaseST(DBBase):
    obj_type = __name__
    _indexed_by_name = True
    _uuid_fq_name_map = {}

    def evaluate(self):
        # Implement in the derived class
        pass

    @classmethod
    def locate(cls, key, *args):
        obj = super(DBBaseST, cls).locate(key, *args)
        if obj.obj.uuid not in cls._uuid_fq_name_map:
            cls._uuid_fq_name_map[obj.obj.uuid] = key
        return obj
    # end locate

    @classmethod
    def delete(cls, key):
        obj = cls.get(key)
        if obj is None:
            return
        if obj.obj.uuid in cls._uuid_fq_name_map:
            del cls._uuid_fq_name_map[obj.obj.uuid]
        obj.delete_obj()
        del cls._dict[key]
    # end delete

    @classmethod
    def get_by_uuid(cls, uuid, *args):
        return cls.get(cls._uuid_fq_name_map.get(uuid))
# end DBBaseST


class GlobalSystemConfigST(DBBaseST):
    _dict = {}
    obj_type = 'global_system_config'
    _autonomous_system = 0
    _ibgp_auto_mesh = None

    def __init__(self, uuid, obj):
        self.name = uuid
        self.uuid = uuid
        self.update(obj)
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(uuid=self.uuid)
        self.update_autonomous_system(self.obj.autonomous_system)
        self.update_ibgp_auto_mesh(self.obj.ibgp_auto_mesh)
    # end update

    @classmethod
    def get_autonomous_system(cls):
        return cls._autonomous_system
    # end get_autonomous_system

    @classmethod
    def get_ibgp_auto_mesh(cls):
        return cls._ibgp_auto_mesh
    # end get_ibgp_auto_mesh

    @classmethod
    def update_autonomous_system(cls, new_asn):
        # ifmap reports as string, obj read could return it as int
        if int(new_asn) == cls._autonomous_system:
            return
        cls._old_asn = cls._autonomous_system
        cls._autonomous_system = int(new_asn)
    # end update_autonomous_system

    def evaluate(self):
        for vn in VirtualNetworkST.values():
            vn.update_autonomous_system(self._old_asn, self._autonomous_system)
        for router in BgpRouterST.values():
            router.update_global_asn(self._autonomous_system)
            router.update_peering()
        # end for router
        for router in LogicalRouterST.values():
            router.update_autonomous_system(self._autonomous_system)
        # end for router
        self._old_asn = self._autonomous_system
    # end evaluate

    @classmethod
    def update_ibgp_auto_mesh(cls, value):
        if value is None:
            value = True
        cls._ibgp_auto_mesh = value
    # end update_ibgp_auto_mesh

# end GlobalSystemConfigST


# a struct to store attributes related to Virtual Networks needed by
# schema transformer


class VirtualNetworkST(DBBaseST):
    _dict = {}
    obj_type = 'virtual_network'

    def __init__(self, name, obj=None, acl_dict=None, ri_dict=None):
        self.obj = obj or self.read_vnc_obj(fq_name=name)
        self.name = name
        self.uuid = self.obj.uuid
        self.network_policys = OrderedDict()
        self.virtual_machine_interfaces = set()
        self.connections = set()
        self.rinst = {}
        self.acl = None
        self.dynamic_acl = None
        for acl in self.obj.get_access_control_lists() or []:
            if acl_dict:
                acl_obj = acl_dict[acl['uuid']]
            else:
                acl_obj = self.read_vnc_obj(acl['uuid'],
                                            obj_type='access_control_list')
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
        self.route_tables = set()
        self.routes = {}
        self.service_chains = {}
        prop = self.obj.get_virtual_network_properties(
        ) or VirtualNetworkType()
        self.allow_transit = prop.allow_transit
        nid = self.obj.get_virtual_network_network_id()
        if nid is None:
            nid = prop.network_id or self._cassandra.alloc_vn_id(name) + 1
            self.obj.set_virtual_network_network_id(nid)
            self._vnc_lib.virtual_network_update(self.obj)
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
            ri_obj = self.locate_routing_instance(self._default_ri_name, ri_dict)
            if ri_obj is not None:
                # if primary RI is connected to another primary RI, we need to
                # also create connection between the VNs
                for connection in ri_obj.connections:
                    remote_ri_fq_name = connection.split(':')
                    if remote_ri_fq_name[-1] == remote_ri_fq_name[-2]:
                        self.connections.add(':'.join(remote_ri_fq_name[0:-1] ))

        for ri in getattr(self.obj, 'routing_instances', []):
            ri_name = ri['to'][-1]
            if ri_name not in self.rinst:
                self.locate_routing_instance(ri_name, ri_dict)
        self.update(self.obj)
        self.update_multiple_refs('virtual_machine_interface', self.obj)
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(uuid=self.uuid)

        old_policies = set(self.network_policys.keys())
        self.network_policys = OrderedDict()
        for policy_ref in self.obj.get_network_policy_refs() or []:
            self.add_policy(':'.join(policy_ref['to']), policy_ref.get('attr'))

        for policy_name in NetworkPolicyST.get_internal_policies(self.name):
            self.add_policy(policy_name)
        for policy_name in old_policies - set(self.network_policys.keys()):
            policy = NetworkPolicyST.get(policy_name)
            if not policy:
                continue
            policy.virtual_networks.discard(self.name)

        for policy_name in set(self.network_policys.keys()) - old_policies:
            policy = NetworkPolicyST.get(policy_name)
            if not policy:
                continue
            policy.virtual_networks.add(self.name)

        self.update_multiple_refs('route_table', self.obj)
        self.ipams = {}
        for ipam_ref in self.obj.get_network_ipam_refs() or []:
            subnet = ipam_ref['attr']
            self.ipams[':'.join(ipam_ref['to'])] = subnet
        self.update_ipams()
        self.set_route_target_list(self.obj.get_route_target_list())
    # end update

    @staticmethod
    def _get_service_id_from_ri(ri_name):
        if not ri_name.startswith('service-'):
            return None
        sc_id = ri_name[45:].replace('_', ':')
        return sc_id
    # end _get_service_id_from_ri

    def delete_obj(self):
        for policy_name in self.network_policys:
            policy = NetworkPolicyST.get(policy_name)
            if not policy:
                continue
            policy.virtual_networks.discard(self.name)

        self.update_multiple_refs('virtual_machine_interface', {})
        for service_chain_list in self.service_chains.values():
            for service_chain in service_chain_list:
                service_chain.destroy()
        for ri in self.rinst.values():
            self.delete_routing_instance(ri)
        if self.acl:
            self._vnc_lib.access_control_list_delete(id=self.acl.uuid)
        if self.dynamic_acl:
            self._vnc_lib.access_control_list_delete(id=self.dynamic_acl.uuid)
        nid = self.obj.get_virtual_network_network_id()
        if nid is None:
            props = self.obj.get_virtual_network_properties()
            if props:
                nid = props.network_id
        if nid:
            self._cassandra.free_vn_id(nid - 1)
        for policy in NetworkPolicyST.values():
            if self.name in policy.analyzer_vn_set:
                policy.analyzer_vn_set.discard(self.name)

        self.update_multiple_refs('route_table', {})
        self.update_route_table()
        self.uve_send(deleted=True)
    # end delete

    def update_autonomous_system(self, old_asn, new_asn):
        if (self.obj.get_fq_name() in
                [common.IP_FABRIC_VN_FQ_NAME,
                 common.LINK_LOCAL_VN_FQ_NAME]):
            # for ip-fabric and link-local VN, we don't need to update asn
            return
        ri = self.get_primary_routing_instance()
        ri_fq_name = ri.get_fq_name_str()
        rtgt_num = self._cassandra.get_route_target(ri_fq_name)
        old_rtgt_name = "target:%d:%d" % (old_asn, rtgt_num)
        new_rtgt_name = "target:%s:%d" % (new_asn, rtgt_num)
        new_rtgt_obj = RouteTargetST.locate(new_rtgt_name)
        old_rtgt_obj = RouteTarget(old_rtgt_name)
        inst_tgt_data = InstanceTargetType()
        ri.obj = ri.read_vnc_obj(fq_name=ri_fq_name)
        ri.obj.del_route_target(old_rtgt_obj)
        ri.obj.add_route_target(new_rtgt_obj.obj, inst_tgt_data)
        self._vnc_lib.routing_instance_update(ri.obj)
        for (prefix, nexthop) in self.routes.items():
            left_ri = self._get_routing_instance_from_route(nexthop)
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
            self._vnc_lib.routing_instance_update(left_ri.obj)
        try:
            RouteTargetST.delete(old_rtgt_obj.get_fq_name()[0])
        except RefsExistError:
            # if other routing instances are referring to this target,
            # it will be deleted when those instances are deleted
            pass
    # end update_autonomous_system

    def add_policy(self, policy_name, attrib=None):
        # Add a policy ref to the vn. Keep it sorted by sequence number
        if attrib is None:
            attrib = VirtualNetworkPolicyType(SequenceType(sys.maxint,
                                                           sys.maxint))
        if attrib.sequence is None:
            self._logger.error("Cannot assign policy %s to %s: sequence "
                               "number is not available", policy_name,
                               self.name)
            return

        self.network_policys[policy_name] = attrib
        self.network_policys = OrderedDict(sorted(self.network_policys.items(),
                                           key=lambda t:(t[1].sequence.major,
                                                         t[1].sequence.minor)))
    # end add_policy

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
        service_chain_list = self.service_chains.setdefault(remote_vn, [])
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
            return sc_ip_address
        try:
            sc_ip_address = self._vnc_lib.virtual_network_ip_alloc(
                self.obj, count=1)[0]
        except (NoIdError, RefsExistError) as e:
            self._logger.error(
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
            self._vnc_lib.virtual_network_ip_free(self.obj, [sc_ip_address])
        except NoIdError:
            pass
    # end free_service_chain_ip

    def get_route_target(self):
        return "target:%s:%d" % (GlobalSystemConfigST.get_autonomous_system(),
                                 self._route_target)
    # end get_route_target

    def _ri_needs_external_rt(self, ri_name):
        sc_id = self._get_service_id_from_ri(ri_name)
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
            self._logger.error("Cannot read routing instance %s",
                               rinst_fq_name_str)
            return None
        rinst = RoutingInstanceST(rinst_obj)
        self.rinst[rinst_name] = rinst
        return rinst
    # end locate_routing_instance_no_target

    def locate_routing_instance(self, rinst_name, ri_dict=None):
        if rinst_name in self.rinst:
            return self.rinst[rinst_name]

        is_default = (rinst_name == self._default_ri_name)
        rinst_fq_name_str = '%s:%s' % (self.obj.get_fq_name_str(), rinst_name)
        old_rtgt = self._cassandra.get_route_target(rinst_fq_name_str)
        rtgt_num = self._cassandra.alloc_route_target(rinst_fq_name_str)

        rt_key = "target:%s:%d" % (GlobalSystemConfigST.get_autonomous_system(),
                                   rtgt_num)
        rtgt_obj = RouteTargetST.locate(rt_key).obj
        if is_default:
            inst_tgt_data = InstanceTargetType()
        elif self._ri_needs_external_rt(rinst_name):
            inst_tgt_data = InstanceTargetType(import_export="export")
        else:
            inst_tgt_data = None

        try:
            try:
                if ri_dict:
                    rinst_obj = ri_dict[rinst_fq_name_str]
                else:
                    rinst_obj = self.read_vnc_obj(
                        fq_name=rinst_fq_name_str, obj_type='routing_instance')
                if rinst_obj.parent_uuid != self.obj.uuid:
                    # Stale object. Delete it.
                    self._vnc_lib.routing_instance_delete(id=rinst_obj.uuid)
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
                    if (not compare_refs(rinst_obj.get_route_target_refs(),
                                         old_rt_refs)):
                        need_update = True
                    if need_update:
                        self._vnc_lib.routing_instance_update(rinst_obj)
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
                self._vnc_lib.routing_instance_create(rinst_obj)
        except (BadRequest, HttpError) as e:
            self._logger.error(
                "Error while creating routing instance: " + str(e))
            return None

        if rinst_obj.name == self._default_ri_name:
            self._route_target = rtgt_num

        rinst = RoutingInstanceST(rinst_obj, rt_key)
        self.rinst[rinst_name] = rinst

        if 0 < old_rtgt < common.BGP_RTGT_MIN_ID:
            rt_key = "target:%s:%d" % (
                GlobalSystemConfigST.get_autonomous_system(), old_rtgt)
            RouteTargetST.delete(rt_key)
        return rinst
    # end locate_routing_instance

    def delete_routing_instance(self, ri):
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
                ri_name = self.get_service_name(service_chain.name,
                                                service_chain.service_list[0])
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
                    ri.update_route_target_list([], [self.get_route_target()],
                                                import_export='export')
    # end set_properties

    def set_route_target_list(self, rt_list):
        ri = self.get_primary_routing_instance()
        old_rt_list = self.rt_list
        if rt_list:
            self.rt_list = set(rt_list.get_route_target())
        else:
            self.rt_list = set()
        rt_add = self.rt_list - old_rt_list
        rt_del = old_rt_list - self.rt_list
        if len(rt_add) == 0 and len(rt_del) == 0:
            return
        for rt in rt_add:
            RouteTargetST.locate(rt)
        ri.update_route_target_list(rt_add, rt_del)
        for ri_obj in self.rinst.values():
            if self._ri_needs_external_rt(ri_obj.name):
                ri_obj.update_route_target_list(rt_add, rt_del,
                                                import_export='export')
        for (prefix, nexthop) in self.routes.items():
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
                self._vnc_lib.routing_instance_update(left_ri.obj)
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
        si = ServiceInstanceST.get(next_hop)
        if si is None:
            self._logger.error("Cannot find service instance %s", next_hop)
            return None
        if not si.auto_policy:
            self._logger.error("%s: route table next hop must be service "
                               "instance with auto policy", self.name)
            return None
        if (not si.left_vn_str or not si.right_vn_str):
            self._logger.error("%s: route table next hop service instance "
                               "must have left and right virtual networks",
                               self.name)
            return None
        left_vn = VirtualNetworkST.get(si.left_vn_str)
        if left_vn is None:
            self._logger.error("Virtual network %s not present",
                               si.left_vn_str)
            return None
        sc = ServiceChain.find(si.left_vn_str, si.right_vn_str, '<>',
                               [PortType()], [PortType()], 'any')
        if sc is None:
            self._logger.error("Service chain between %s and %s not present",
                               si.left_vn_str, si.right_vn_str)
            return None
        left_ri_name = left_vn.get_service_name(sc.name, next_hop)
        return left_vn.rinst.get(left_ri_name)
    # end _get_routing_instance_from_route

    def add_route(self, prefix, next_hop):
        self.routes[prefix] = next_hop
        left_ri = self._get_routing_instance_from_route(next_hop)
        if left_ri is None:
            self._logger.error(
                "left routing instance is none for %s", next_hop)
            return
        service_info = left_ri.obj.get_service_chain_information()
        if service_info is None:
            self._logger.error(
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
            self._vnc_lib.routing_instance_update(left_ri.obj)
        left_ri.update_route_target_list(
            rt_add=self.rt_list | set([self.get_route_target()]),
            import_export="import")
    # end add_route

    def delete_route(self, prefix):
        if prefix not in self.routes:
            return
        next_hop = self.routes[prefix]
        del self.routes[prefix]
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
            self._vnc_lib.routing_instance_update(left_ri.obj)
    # end delete_route

    def update_route_table(self):
        stale = {}
        for prefix in self.routes:
            stale[prefix] = True
        for rt_name in self.route_tables:
            route_table = RouteTableST.get(rt_name)
            if route_table is None or route_table.routes is None:
                continue
            for route in route_table.routes or []:
                if route.prefix in self.routes:
                    stale.pop(route.prefix, None)
                    if route.next_hop == self.routes[route.prefix]:
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
                                                  sandesh=self._sandesh)
            vn_msg.send(sandesh=self._sandesh)
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
        # end for
        for rhs in self.service_chains:
            rhs_vn = VirtualNetworkST.get(rhs)
            if rhs_vn and self.name in rhs_vn.service_chains:
                # two way connection
                vn_trace.connected_networks.append(rhs)
            else:
                # one way connection
                vn_trace.partially_connected_networks.append(rhs)
        # end for
        vn_msg = UveVirtualNetworkConfigTrace(data=vn_trace, sandesh=self._sandesh)
        vn_msg.send(sandesh=self._sandesh)
    # end uve_send

    def get_service_name(self, sc_name, si_name):
        name = "service-" + sc_name + "-" + si_name
        return name.replace(':', '_')
    # end get_service_name

    @staticmethod
    def get_analyzer_vn_and_ip(analyzer_name):
        vn_analyzer = None
        ip_analyzer = None
        si = ServiceInstanceST.get(analyzer_name)
        if si is None:
            return (None, None)
        vm_analyzer = list(si.virtual_machines)
        if not vm_analyzer:
            return (None, None)
        vm_analyzer_obj = VirtualMachineST.get(vm_analyzer[0])
        if vm_analyzer_obj is None:
            return (None, None)
        vmis = vm_analyzer_obj.virtual_machine_interfaces
        for vmi_name in vmis:
            vmi = VirtualMachineInterfaceST.get(vmi_name)
            if vmi and vmi.service_interface_type == 'left':
                vn_analyzer = vmi.virtual_network
                ip_analyzer = vmi.get_any_instance_ip_address()
                break
        # end for vmi_ref
        return (vn_analyzer, ip_analyzer)
    # end get_analyzer_vn_and_ip

    def process_analyzer(self, action):
        analyzer_name = action.mirror_to.analyzer_name
        try:
            (vn_analyzer, ip) = self.get_analyzer_vn_and_ip(analyzer_name)
            if ip:
                action.mirror_to.set_analyzer_ip_address(ip)
            if vn_analyzer:
                self._logger.debug("Mirror: adding connection: %s to %s",
                                   self.name, vn_analyzer)
                self.add_connection(vn_analyzer)
                vn_obj = VirtualNetworkST.get(vn_analyzer)
                if vn_obj:
                    vn_obj.add_connection(self.name)
            else:
                self._logger.error("Mirror: %s: no analyzer vn for %s",
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
            self._logger.error("No action specified in policy rule "
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
                    if self.name in dp_obj.virtual_networks:
                        remote_network_name = svn
                        daddr_match.network_policy = None
                        daddr_match.virtual_network = self.name
                    else:
                        self._logger.error(
                            "network policy rule attached to %s has src = %s,"
                            " dst = %s. Ignored.", self.name, svn or spol,
                            dvn or dpol)
                        continue

                elif not svn and spol:
                    sp_obj = NetworkPolicyST.get(spol)
                    if self.name in sp_obj.virtual_networks:
                        remote_network_name = dvn
                        daddr_match.network_policy = None
                        saddr_match.virtual_network = self.name
                    else:
                        self._logger.error(
                            "network policy rule attached to %s has src = %s,"
                            " dst = %s. Ignored.", self.name, svn or spol,
                            dvn or dpol)
                        continue
                elif (not svn and not dvn and not spol and not dpol and
                      s_cidr and d_cidr):
                    if prule.action_list.apply_service:
                        self._logger.error(
                            "service chains not allowed in cidr only rules "
                            "network %s, src = %s, dst = %s. Ignored.",
                            self.name, s_cidr, d_cidr)
                        continue
                else:
                    self._logger.error("network policy rule attached to %s"
                                       "has svn = %s, dvn = %s. Ignored.",
                                       self.name, svn, dvn)
                    continue

                service_list = None
                if prule.action_list.apply_service != []:
                    if remote_network_name == self.name:
                        self._logger.error("Service chain source and dest "
                                           "vn are same: %s", self.name)
                        continue
                    remote_vn = VirtualNetworkST.get(remote_network_name)
                    if remote_vn is None:
                        self._logger.error(
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
                        else:
                            action.assign_routing_instance = None

                        if action.mirror_to and action.mirror_to.analyzer_name:
                            if ((svn in [self.name, 'any']) or
                                    (dvn in [self.name, 'any'])):
                                self.process_analyzer(action)

                        if saddr_match.network_policy:
                            pol = NetworkPolicyST.get(saddr_match.network_policy)
                            if not pol:
                                self._logger.error(
                                    "Policy %s not found while applying policy "
                                    "to network %s", saddr_match.network_policy,
                                    self.name)
                                continue
                            sa_list = [AddressType(virtual_network=x)
                                       for x in pol.virtual_networks]
                        else:
                            sa_list = [saddr_match]

                        if daddr_match.network_policy:
                            pol = NetworkPolicyST.get(daddr_match.network_policy)
                            if not pol:
                                self._logger.error(
                                    "Policy %s not found while applying policy "
                                    "to network %s", daddr_match.network_policy,
                                    self.name)
                                continue
                            da_list = [AddressType(virtual_network=x)
                                       for x in pol.virtual_networks]
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

    def evaluate(self):
        old_virtual_network_connections = self.expand_connections()
        old_service_chains = self.service_chains
        self.connections = set()
        self.service_chains = {}

        static_acl_entries = None
        dynamic_acl_entries = None
        for policy_name in self.network_policys:
            timer = self.network_policys[policy_name].get_timer()
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
                acl_rule_list = self.policy_to_acl_rule(prule, dynamic)
                acl_rule_list.update_acl_entries(acl_entries)
                for arule in acl_rule_list.get_list():
                    match = arule.get_match_condition()
                    action = arule.get_action_list()
                    if action.simple_action == 'deny':
                        continue
                    connected_network = None
                    if (match.dst_address.virtual_network in [self.name, "any"]):
                        connected_network = match.src_address.virtual_network
                    elif (match.src_address.virtual_network in [self.name, "any"]):
                        connected_network = match.dst_address.virtual_network
                    if action.apply_service:
                        # if a service was applied, the ACL should have a
                        # pass action, and we should not make a connection
                        # between the routing instances
                        action.simple_action = "pass"
                        action.apply_service = []
                        continue

                    if connected_network and action.simple_action:
                        self.add_connection(connected_network)

                # end for acl_rule_list
            # end for policy_rule_entries.policy_rule
        # end for self.network_policys

        if static_acl_entries is not None:
            # if a static acl is created, then for each rule, we need to
            # add a deny rule and add a my-vn to any allow rule in the end
            acl_list = AclRuleListST()

            # Create my-vn to my-vn allow
            match = MatchConditionType(
                "any", AddressType(virtual_network=self.name), PortType(),
                AddressType(virtual_network=self.name), PortType())
            action = ActionListType("pass")
            acl = AclRuleType(match, action)
            acl_list.append(acl)

            for rule in static_acl_entries.get_acl_rule():
                match = MatchConditionType(
                    "any", rule.match_condition.src_address, PortType(),
                    rule.match_condition.dst_address, PortType())

                acl = AclRuleType(match, ActionListType("deny"),
                                  rule.get_rule_uuid())
                acl_list.append(acl)

                match = MatchConditionType(
                    "any", rule.match_condition.dst_address, PortType(),
                    rule.match_condition.src_address, PortType())

                acl = AclRuleType(match, ActionListType("deny"),
                                  rule.get_rule_uuid())
                acl_list.append(acl)
            # end for rule

            # Create any-vn to any-vn allow
            match = MatchConditionType(
                "any", AddressType(virtual_network="any"), PortType(),
                AddressType(virtual_network="any"), PortType())
            action = ActionListType("pass")
            acl = AclRuleType(match, action)
            acl_list.append(acl)
            acl_list.update_acl_entries(static_acl_entries)

        self.acl = _access_control_list_update(self.acl, self.obj.name,
                                               self.obj, static_acl_entries)
        self.dynamic_acl = _access_control_list_update(self.dynamic_acl,
                                                       'dynamic', self.obj,
                                                       dynamic_acl_entries)

        for vmi_name in self.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceST.get(vmi_name)
            if (vmi and vmi.interface_mirror is not None and
                vmi.interface_mirror.mirror_to is not None and
                vmi.interface_mirror.mirror_to.analyzer_name is not None):
                    vmi.process_analyzer()

        # This VN could be the VN for an analyzer interface. If so, we need
        # to create a link from all VNs containing a policy with that
        # analyzer
        for policy in NetworkPolicyST.values():
            for prule in policy.rules:
                if (prule.action_list is None or
                    prule.action_list.mirror_to is None or
                    prule.action_list.mirror_to.analyzer_name is None):
                    continue
                (vn_analyzer, _) = VirtualNetworkST.get_analyzer_vn_and_ip(
                    prule.action_list.mirror_to.analyzer_name)
                if vn_analyzer != self.name:
                    continue
                for net_name in policy.virtual_networks:
                    net = VirtualNetworkST.get(net_name)
                    if net is not None:
                        self.add_connection(net_name)

        # Derive connectivity changes between VNs
        new_connections = self.expand_connections()
        for network in old_virtual_network_connections - new_connections:
            self.delete_ri_connection(network)
        for network in new_connections - old_virtual_network_connections:
            self.add_ri_connection(network)

        # copy the routing instance connections from old to new
        for network in old_virtual_network_connections & new_connections:
            try:
                ri1 = self.get_primary_routing_instance()
                vn2 = VirtualNetworkST.get(network)
                ri2 = vn2.get_primary_routing_instance()
                ri1.connections.add(ri2.get_fq_name_str())
            except Exception:
                pass

        # First create the newly added service chains
        for remote_vn_name in self.service_chains:
            remote_vn = VirtualNetworkST.get(remote_vn_name)
            if remote_vn is None:
                continue
            remote_service_chain_list = remote_vn.service_chains.get(self.name)
            service_chain_list = self.service_chains[remote_vn_name]
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
            remote_service_chain_list = remote_vn.service_chains.get(self.name)
            service_chain_list = old_service_chains[remote_vn_name]
            for service_chain in service_chain_list or []:
                if service_chain in (self.service_chains.get(remote_vn_name)
                                     or []):
                    continue
                if service_chain in (remote_service_chain_list or []):
                    service_chain.destroy()
                else:
                    service_chain.delete()
        # for remote_vn_name

        self.update_route_table()
    # end evaluate
# end class VirtualNetworkST


class RouteTargetST(DBBaseST):
    _dict = {}
    obj_type = 'route_target'

    def __init__(self, rt_key, obj=None):
        self.name = rt_key
        try:
            self.obj = obj or self.read_vnc_obj(fq_name=[rt_key])
        except NoIdError:
            self.obj = RouteTarget(rt_key)
            self._vnc_lib.route_target_create(self.obj)
    # end __init__

    def update(self, obj=None):
        pass

    def delete_obj(self):
        self._vnc_lib.route_target_delete(fq_name=[self.name])
    # end delete_obj
# end RoutTargetST

# a struct to store attributes related to Network Policy needed by schema
# transformer


class NetworkPolicyST(DBBaseST):
    _dict = {}
    obj_type = 'network_policy'
    _internal_policies = set()
    _service_instances = {}
    _network_policys = {}

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_networks = set()
        self.service_instances = set()
        self.internal = False
        self.rules = []
        self.analyzer_vn_set = set()

        # policies referred in this policy as src or dst
        self.referred_policies = set()
        # policies referring to this policy as src or dst
        self.update(obj)
        for vn_ref in self.obj.get_virtual_network_back_refs() or []:
            vn_name = ':'.join(vn_ref['to'])
            self.virtual_networks.add(vn_name)
            vn = VirtualNetworkST.get(vn_name)
            if vn is None:
                continue
            vnp = VirtualNetworkPolicyType(**vn_ref['attr'])
            vn.add_policy(name, vnp)
        self.network_policys = NetworkPolicyST._network_policys.get(name, set())
    # end __init__

    def set_internal(self):
        self.internal = True
        self._internal_policies.add(self.name)
    # end set_internal

    @classmethod
    def get_internal_policies(cls, vn_name):
        policies = []
        for policy_name in cls._internal_policies:
            policy = NetworkPolicyST.get(policy_name)
            if policy is None:
                continue
            if vn_name in policy.virtual_networks:
                policies.append(policy.name)
        return policies

    @classmethod
    def get_by_service_instance(cls, si_name):
        return cls._service_instances.get(si_name, set())

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(fq_name=self.name)
        self.add_rules(self.obj.get_network_policy_entries())
    # end update

    def add_rules(self, entries):
        if entries is None:
            self.rules = []
        else:
            self.rules = entries.policy_rule
        np_set = set()
        self.analyzer_vn_set = set()
        si_set = set()
        for prule in self.rules:
            if prule.action_list is None:
                continue
            if (prule.action_list.mirror_to and
                prule.action_list.mirror_to.analyzer_name):
                (vn, _) = VirtualNetworkST.get_analyzer_vn_and_ip(
                    prule.action_list.mirror_to.analyzer_name)
                if vn:
                    self.analyzer_vn_set.add(vn)
                si_set.add(prule.action_list.mirror_to.analyzer_name)
            if prule.action_list.apply_service:
                si_set = si_set.union(prule.action_list.apply_service)
            for addr in prule.src_addresses + prule.dst_addresses:
                if addr.network_policy:
                    np_set.add(addr.network_policy)
        # end for prule

        for policy_name in self.referred_policies - np_set:
            policy_set = self._network_policys.get(policy_name)
            if policy_set is None:
                continue
            policy_set.discard(self.name)
            if not policy_set:
                del self._network_policys[policy_name]
            policy = NetworkPolicyST.get(policy_name)
            if policy:
                policy.network_policys = policy_set
        for policy_name in np_set - self.referred_policies:
            policy_set = self._network_policys.setdefault(policy_name, set())
            policy_set.add(self.name)
            policy = NetworkPolicyST.get(policy_name)
            if policy:
                policy.network_policys = policy_set
        self.referred_policies = np_set
        self.update_service_instances(si_set)
    # end add_rules

    def update_service_instances(self, si_set):
        old_si_set = self.service_instances
        for si_name in old_si_set - si_set:
            si_list = self._service_instances.get(si_name)
            if si_list:
                si_list.discard(self.name)
            if not si_list:
                del self._service_instances[si_name]
            si = ServiceInstanceST.get(si_name)
            if si is None:
                continue
            si.network_policys.discard(self.name)

        for si_name in si_set - old_si_set:
            si_list = self._service_instances.setdefault(si_name, set())
            si_list.add(self.name)
            si = ServiceInstanceST.get(si_name)
            if si is None:
                continue
            si.network_policys.add(self.name)
        self.service_instances = si_set
    # update_service_instances

    def delete_obj(self):
        self.add_rules(None)
        self._internal_policies.discard(self.name)
        for vn_name in self.virtual_networks:
            vn = VirtualNetworkST.get(vn_name)
            if vn is None:
                continue
            if self.name in vn.network_policys:
                del vn.network_policys[self.name]
# end class NetworkPolicyST


class RouteTableST(DBBaseST):
    _dict = {}
    obj_type = 'route_table'
    _service_instances = {}

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_networks = set()
        self.service_instances = set()
        self.routes = []
        self.update(obj)
        self.update_multiple_refs('virtual_network', self.obj)
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(fq_name=self.name)
        routes = self.obj.get_routes()
        if routes:
            self.routes = routes.get_route() or []
        si_set = set(route.next_hop for route in self.routes)
        self.update_service_instances(si_set)
    # end update

    def update_service_instances(self, si_set):
        old_si_set = self.service_instances
        for si_name in old_si_set - si_set:
            rt_list = self._service_instances.get(si_name)
            if rt_list:
                rt_list.discard(self.name)
            if not rt_list:
                del self._service_instances[si_name]
            si = ServiceInstanceST.get(si_name)
            if si is None:
                continue
            si.route_tables.discard(self.name)

        for si_name in si_set - old_si_set:
            rt_list = self._service_instances.setdefault(si_name, set())
            rt_list.add(self.name)
            si = ServiceInstanceST.get(si_name)
            if si is None:
                continue
            si.route_tables.add(self.name)
        self.service_instances = si_set
    # end update_service_instances

    @classmethod
    def get_by_service_instance(cls, si_name):
        return cls._service_instances.get(si_name, set())

    def delete_obj(self):
        self.update_multiple_refs('virtual_network', {})
# end RouteTableST

# a struct to store attributes related to Security Group needed by schema
# transformer


class SecurityGroupST(DBBaseST):
    _dict = {}
    obj_type = 'security_group'

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
                self._vnc_lib.access_control_list_update(acl)
    # end update_acl

    def __init__(self, name, obj=None, acl_dict=None):
        def _get_acl(uuid):
            if acl_dict:
                return acl_dict[uuid]
            return self.read_vnc_obj(uuid, obj_type='access_control_list')

        self.name = name
        self.obj = obj or self.read_vnc_obj(fq_name=name)
        self.uuid = self.obj.uuid
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
                self._vnc_lib.access_control_list_delete(id=acl['uuid'])
        self.update(self.obj)
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(uuid=self.uuid)
        self.rule_entries = self.obj.get_security_group_entries()
        config_id = self.obj.get_configured_security_group_id() or 0
        self.set_configured_security_group_id(config_id, False)
    # end update

    def set_configured_security_group_id(self, config_id, update_acl=True):
        if self.config_sgid == config_id:
            return
        self.config_sgid = config_id
        sg_id = self.obj.get_security_group_id()
        if sg_id is not None:
            sg_id = int(sg_id)
        if config_id:
            if sg_id is not None:
                if sg_id > SGID_MIN_ALLOC:
                    self._cassandra.free_sg_id(sg_id - SGID_MIN_ALLOC)
                else:
                    if self.name == self._cassandra.get_sg_from_id(sg_id):
                        self._cassandra.free_sg_id(sg_id)
            self.obj.set_security_group_id(str(config_id))
        else:
            do_alloc = False
            if sg_id is not None:
                if sg_id < SGID_MIN_ALLOC:
                    if self.name == self._cassandra.get_sg_from_id(sg_id):
                        self.obj.set_security_group_id(sg_id + SGID_MIN_ALLOC)
                    else:
                        do_alloc = True
            else:
                do_alloc = True
            if do_alloc:
                sg_id_num = self._cassandra.alloc_sg_id(self.name)
                self.obj.set_security_group_id(sg_id_num + SGID_MIN_ALLOC)
        if sg_id != int(self.obj.get_security_group_id()):
            self._vnc_lib.security_group_update(self.obj)
        from_value = self.sg_id or self.name
        if update_acl:
            for sg in self._dict.values():
                sg.update_acl(from_value=from_value,
                              to_value=self.obj.get_security_group_id())
        # end for sg
        self.sg_id = self.obj.get_security_group_id()
    # end set_configured_security_group_id

    def delete_obj(self):
        if self.ingress_acl:
            self._vnc_lib.access_control_list_delete(id=self.ingress_acl.uuid)
        if self.egress_acl:
            self._vnc_lib.access_control_list_delete(id=self.egress_acl.uuid)
        sg_id = self.obj.get_security_group_id()
        if sg_id is not None and not self.config_sgid:
            if sg_id < SGID_MIN_ALLOC:
                self._cassandra.free_sg_id(sg_id)
            else:
                self._cassandra.free_sg_id(sg_id-SGID_MIN_ALLOC)
        for sg in self._dict.values():
            if self.name == sg.name:
                continue
            sg.update_acl(from_value=sg_id, to_value=self.name)
        # end for sg
    # end delete_obj

    def evaluate(self):
        self.update_policy_entries()

    def update_policy_entries(self):
        ingress_acl_entries = AclEntriesType()
        egress_acl_entries = AclEntriesType()

        if self.rule_entries:
            prules = self.rule_entries.get_policy_rule() or []
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

    def __init__(self, ri_obj, route_target=None):
        self.name = ri_obj.name
        self.obj = ri_obj
        self.service_chain = VirtualNetworkST._get_service_id_from_ri(self.name)
        self.route_target = route_target
        self.connections = set()
        for ri_ref in self.obj.get_routing_instance_refs() or []:
            self.connections.add(':'.join(ri_ref['to']))
    # end __init__

    def read_vnc_obj(self, uuid=None, fq_name=None, obj_type=None):
        return DBBaseST().read_vnc_obj(uuid, fq_name,
                                       obj_type or 'routing_instance')
    # end read_vnc_obj

    def get_fq_name(self):
        return self.obj.get_fq_name()
    # end get_fq_name

    def get_fq_name_str(self):
        return self.obj.get_fq_name_str()
    # end get_fq_name_str

    def add_connection(self, ri2):
        self.connections.add(ri2.get_fq_name_str())
        ri2.connections.add(self.get_fq_name_str())
        self.obj = self.read_vnc_obj(self.obj.uuid)
        ri2.obj = self.read_vnc_obj(ri2.obj.uuid)

        conn_data = ConnectionType()
        self.obj.add_routing_instance(ri2.obj, conn_data)
        DBBaseST._vnc_lib.routing_instance_update(self.obj)
    # end add_connection

    def delete_connection(self, ri2):
        self.connections.discard(ri2.get_fq_name_str())
        ri2.connections.discard(self.get_fq_name_str())
        try:
            self.obj = self.read_vnc_obj(self.obj.uuid)
        except NoIdError:
            return
        self.obj.del_routing_instance(ri2.obj)
        DBBaseST._vnc_lib.routing_instance_update(self.obj)
    # end delete_connection

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
            rtgt_obj = RouteTargetST.locate(rt).obj
            inst_tgt_data = InstanceTargetType(import_export=import_export)
            self.obj.add_route_target(rtgt_obj, inst_tgt_data)
        for rt in rt_del or set():
            rtgt_obj = RouteTarget(rt)
            self.obj.del_route_target(rtgt_obj)
        if len(rt_add) or len(rt_del or set()):
            DBBaseST._vnc_lib.routing_instance_update(self.obj)
    # end update_route_target_list

    def update_static_routes(self, si_name):
        route_tables = RouteTableST.get_by_service_instance(si_name)
        service_info = self.obj.get_service_chain_information()
        if service_info is None:
            self._logger.error(
                "Service chain info not found for %s", self.name)
            return
        sc_address = service_info.get_service_chain_address()
        static_routes = StaticRouteEntriesType()
        all_route_targets = set()
        for route_table_name in route_tables:
            route_table = RouteTableST.get(route_table_name)
            if route_table is None or not route_table.routes:
                continue
            route_targets = set()
            for vn_name in route_table.virtual_networks:
                vn = VirtualNetworkST.get(vn_name)
                if vn is None:
                    continue
                route_targets = route_targets.union(vn.rt_list)
                route_targets.add(vn.get_route_target())
            for route in route_table.routes:
                if route.next_hop != si_name:
                    continue
                static_route = StaticRouteType(prefix=route.prefix,
                                               next_hop=sc_address,
                                               route_target=list(route_targets))
                static_routes.add_route(static_route)
                all_route_targets != route_targets

        self.obj.set_static_route_entries(static_routes)
        self.update_route_target_list(rt_add=all_route_targets, import_export="import")
    # end update_static_routes

    def delete(self, vn_obj=None):
        # refresh the ri object because it could have changed
        self.obj = self.read_vnc_obj(self.obj.uuid, obj_type='routing_instance')
        rtgt_list = self.obj.get_route_target_refs()
        ri_fq_name_str = self.obj.get_fq_name_str()
        DBBaseST._cassandra.free_route_target(ri_fq_name_str)

        service_chain = self.service_chain
        if vn_obj is not None and service_chain is not None:
            # self.free_service_chain_ip(service_chain)
            vn_obj.free_service_chain_ip(self.obj.name)

            uve = UveServiceChainData(name=service_chain, deleted=True)
            uve_msg = UveServiceChain(data=uve, sandesh=DBBaseST._sandesh)
            uve_msg.send(sandesh=DBBaseST._sandesh)

        vmi_refs = getattr(self.obj, 'virtual_machine_interface_back_refs', [])
        for vmi in vmi_refs:
            try:
                vmi_obj = self.read_vnc_obj(
                    vmi['uuid'], obj_type='virtual_machine_interface')
                if service_chain is not None:
                    DBBaseST._cassandra.free_service_chain_vlan(
                        vmi_obj.get_parent_fq_name_str(), service_chain)
                vmi_obj.del_routing_instance(self.obj)
                DBBaseST._vnc_lib.virtual_machine_interface_update(vmi_obj)
            except NoIdError:
                continue

        # end for vmi
        DBBaseST._vnc_lib.routing_instance_delete(id=self.obj.uuid)
        for rtgt in rtgt_list or []:
            try:
                RouteTargetST.delete(rtgt['to'][0])
            except RefsExistError:
                # if other routing instances are referring to this target,
                # it will be deleted when those instances are deleted
                pass

# end class RoutingInstanceST


class ServiceChain(DBBaseST):
    _dict = {}
    obj_type = 'service_chain'

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
            service_ri = vn1.locate_routing_instance(service_name)
            if service_ri is None:
                continue
            service_ri.add_service_info(vn2, service)
            self._vnc_lib.routing_instance_update(service_ri.obj)
        # end for service
    # end update_ipams

    def log_error(self, msg):
        self.error_msg = msg
        self._logger.error('service chain %s: ' + msg)
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
            si = ServiceInstanceST.get(service)
            if si is None:
                self.log_error("Service instance %s not found " + service)
                return None
            vm_list = si.virtual_machines
            if not vm_list:
                self.log_error("No vms found for service instance " + service)
                return None
            mode = si.get_service_mode()
            if mode is None:
                self.log_error("service mode not found: %s" % service)
                return None
            vm_info_list = []
            for service_vm in vm_list:
                vm_obj = VirtualMachineST.get(service_vm)
                if vm_obj is None:
                    self.log_error('virtual machine %s not found' % service_vm)
                    return None
                vm_info = {'vm_obj': vm_obj}

                for interface_name in vm_obj.virtual_machine_interfaces:
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
        uve_msg = UveServiceChain(data=uve, sandesh=self._sandesh)
        uve_msg.send(sandesh=self._sandesh)
    # end create

    def _create(self, si_info):
        self.partially_created = True
        vn1_obj = VirtualNetworkST.locate(self.left_vn)
        vn2_obj = VirtualNetworkST.locate(self.right_vn)
        if not vn1_obj or not vn2_obj:
            self.log_error("vn1_obj or vn2_obj is None")
            return

        service_ri2 = vn1_obj.get_primary_routing_instance()
        first_node = True
        for service in self.service_list:
            service_name1 = vn1_obj.get_service_name(self.name, service)
            service_name2 = vn2_obj.get_service_name(self.name, service)
            service_ri1 = vn1_obj.locate_routing_instance(service_name1)
            if service_ri1 is None or service_ri2 is None:
                self.log_error("service_ri1 or service_ri2 is None")
                return
            service_ri2.add_connection(service_ri1)
            service_ri2 = vn2_obj.locate_routing_instance(service_name2)
            if service_ri2 is None:
                self.log_error("service_ri2 is None")
                return

            if first_node:
                first_node = False
                rt_list = vn1_obj.rt_list
                if vn1_obj.allow_transit:
                    rt_list |= set([vn1_obj.get_route_target()])
                service_ri1.update_route_target_list(rt_list,
                                                     import_export='export')

            mode = si_info[service]['mode']
            nat_service = (mode == "in-network-nat")
            transparent = (mode not in ["in-network", "in-network-nat"])
            self._logger.info("service chain %s: creating %s chain",
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
            service_ri1.update_static_routes(service)
            self._vnc_lib.routing_instance_update(service_ri1.obj)
            self._vnc_lib.routing_instance_update(service_ri2.obj)

        rt_list = vn2_obj.rt_list
        if vn2_obj.allow_transit:
            rt_list |= set([vn2_obj.get_route_target()])
        service_ri2.update_route_target_list(rt_list, import_export='export')

        service_ri2.add_connection(vn2_obj.get_primary_routing_instance())

        if not transparent and len(self.service_list) == 1:
            for vn in VirtualNetworkST.values():
                for prefix, nexthop in vn.routes.items():
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
            self._vnc_lib.virtual_machine_interface_update(vmi.obj)
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
            port_list.append("%s-%s" % (sp.start_port, sp.end_port))
        sc.src_ports = ','.join(port_list)
        port_list = []
        for dp in self.dp_list:
            port_list.append("%s-%s" % (dp.start_port, dp.end_port))
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


class BgpRouterST(DBBaseST):
    _dict = {}
    obj_type = 'bgp_router'

    def __init__(self, name, obj=None):
        self.name = name
        self.asn = None
        self.update(obj)
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(fq_name=self.name)
        self.set_params(self.obj.get_bgp_router_parameters())
    # end update

    def set_params(self, params):
        self.vendor = (params.vendor or 'contrail').lower()
        self.identifier = params.identifier
        if self.vendor == 'contrail':
            self.update_global_asn(GlobalSystemConfigST.get_autonomous_system())
        else:
            self.update_autonomous_system(params.autonomous_system)
    # end set_params

    def update_global_asn(self, asn):
        if self.vendor != 'contrail' or self.asn == int(asn):
            return
        router_obj = self.read_vnc_obj(fq_name=self.name)
        params = router_obj.get_bgp_router_parameters()
        params.autonomous_system = int(asn)
        router_obj.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_update(router_obj)
        self.update_autonomous_system(asn)
    # end update_global_asn

    def update_autonomous_system(self, asn):
        if self.asn == int(asn):
            return
        self.asn = int(asn)
        self.update_peering()
    # end update_autonomous_system

    def evaluate(self):
        self.update_peering()

    def update_peering(self):
        if not GlobalSystemConfigST.get_ibgp_auto_mesh():
            return
        global_asn = int(GlobalSystemConfigST.get_autonomous_system())
        if self.asn != global_asn:
            return
        try:
            obj = self.read_vnc_obj(fq_name=self.name)
        except NoIdError as e:
            self._logger.error("NoIdError while reading bgp router "
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
                self._vnc_lib.bgp_router_update(obj)
            except NoIdError as e:
                self._logger.error("NoIdError while updating bgp router "
                                   "%s: %s", self.name, str(e))
    # end update_peering
# end class BgpRouterST


class VirtualMachineInterfaceST(DBBaseST):
    _dict = {}
    _service_vmi_list = []
    obj_type = 'virtual_machine_interface'

    def __init__(self, name, obj=None):
        self.name = name
        self.service_interface_type = None
        self.interface_mirror = None
        self.virtual_network = None
        self.virtual_machine = None
        self.uuid = None
        self.instance_ips = set()
        self.floating_ips = set()
        self.obj = obj or self.read_vnc_obj(fq_name=name)
        self.uuid = self.obj.uuid
        self.update_multiple_refs('instance_ip', self.obj)
        self.update_multiple_refs('floating_ip', self.obj)
        self.vrf_table = jsonpickle.encode(self.obj.get_vrf_assign_table())
        self.update(self.obj)
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(uuid=self.obj.uuid)
        self.update_single_ref('virtual_network', self.obj)
        if self.obj.parent_type == 'virtual-machine':
            self.add_to_parent(self.obj)
            self.virtual_machine = self.obj.get_parent_fq_name_str()
        else:
            self.update_single_ref('virtual_machine', self.obj)
        props = self.obj.get_virtual_machine_interface_properties()
        if props:
            self.set_service_interface_type(props.get_service_interface_type())
            self.set_interface_mirror(props.get_interface_mirror())
    # end update

    def delete_obj(self):
        self.update_single_ref('virtual_network', {})
        self.update_single_ref('virtual_machine', {})
        self.update_multiple_refs('instance_ip', {})
        self.update_multiple_refs('floating_ip', {})

        try:
            self._service_vmi_list.remove(self)
        except ValueError:
            pass
    # end delete_obj

    def evaluate(self):
        self.set_virtual_network(self.virtual_network)
        self._add_pbf_rules()
        self.process_analyzer()
        self.recreate_vrf_assign_table()
    # end evaluate

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
        if virtual_machine:
            vm = VirtualMachineST.get(virtual_machine)
            if vm:
                vm.add_interface(self.name)
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
            service_ri = vn_obj.locate_routing_instance(service_name)
            sc_ip_address = vn1_obj.allocate_service_chain_ip(
                service_name)
            vlan = self._cassandra.allocate_service_chain_vlan(
                vm_obj.uuid, service_chain.name)

            service_chain.add_pbf_rule(self, service_ri, service_ri,
                                       sc_ip_address, vlan)
    # end _add_pbf_rules

    def set_virtual_network(self, vn_name):
        self.virtual_network = vn_name
        virtual_network = VirtualNetworkST.locate(vn_name)
        if virtual_network is None:
            return
        virtual_network.virtual_machine_interfaces.add(self.name)
        ri = virtual_network.get_primary_routing_instance().obj
        refs = self.obj.get_routing_instance_refs()
        if ri.get_fq_name() not in [r['to'] for r in (refs or [])]:
            self.obj.add_routing_instance(
                ri, PolicyBasedForwardingRuleType(direction="both"))
            try:
                self._vnc_lib.virtual_machine_interface_update(self.obj)
            except NoIdError:
                self._logger.error("NoIdError while updating interface " +
                                   self.name)

        for lr in LogicalRouterST.values():
            if self.name in lr.virtual_machine_interfaces:
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
        if mirror_to == self.interface_mirror.mirror_to:
            return
        vmi_props.set_interface_mirror(self.interface_mirror)
        self.obj.set_virtual_machine_interface_properties(vmi_props)
        try:
            self._vnc_lib.virtual_machine_interface_update(self.obj)
        except NoIdError:
            self._logger.error("NoIdError while updating interface " +
                               self.name)
    # end process_analyzer

    def recreate_vrf_assign_table(self):
        if self.service_interface_type not in ['left', 'right']:
            return
        vn = VirtualNetworkST.get(self.virtual_network)
        if vn is None:
            return
        if self.virtual_machine is None:
            self._logger.error("vm is None for interface %s", self.name)
            return

        vm_obj = VirtualMachineST.get(self.virtual_machine)
        if vm_obj is None:
            self._logger.error(
                "virtual machine %s not found", self.virtual_machine)
            return
        smode = vm_obj.get_service_mode()
        if smode not in ['in-network', 'in-network-nat']:
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
                                    protocol='any',
                                    src_port=PortType(),
                                    dst_port=PortType())

            ri_name = vn.obj.get_fq_name_str() + ':' + vn._default_ri_name
            vrf_rule = VrfAssignRuleType(match_condition=mc,
                                         routing_instance=ri_name,
                                         ignore_acl=False)
            vrf_table.add_vrf_assign_rule(vrf_rule)

        policy_rule_count = 0
        si_name = vm_obj.service_instance
        for service_chain_list in vn.service_chains.values():
            for service_chain in service_chain_list:
                if not service_chain.created:
                    continue
                if si_name not in service_chain.service_list:
                    continue
                ri_name = vn.obj.get_fq_name_str() + ':' + \
                    vn.get_service_name(service_chain.name, si_name)
                for sp in service_chain.sp_list:
                    for dp in service_chain.dp_list:
                        mc = MatchConditionType(src_port=sp,
                                                dst_port=dp,
                                                protocol=service_chain.protocol)

                        vrf_rule = VrfAssignRuleType(
                            match_condition=mc,
                            routing_instance=ri_name,
                            ignore_acl=True)
                        vrf_table.add_vrf_assign_rule(vrf_rule)
                        policy_rule_count += 1
            # end for service_chain
        # end for service_chain_list

        if policy_rule_count == 0:
            vrf_table = None
        vrf_table_pickle = jsonpickle.encode(vrf_table)
        if vrf_table_pickle != self.vrf_table:
            self.obj.set_vrf_assign_table(vrf_table)
            try:
                self._vnc_lib.virtual_machine_interface_update(self.obj)
                self.vrf_table = vrf_table_pickle
            except NoIdError as e:
                if e._unknown_id == self.uuid:
                    VirtualMachineInterfaceST.delete(self.name)

    # end recreate_vrf_assign_table
# end VirtualMachineInterfaceST


class InstanceIpST(DBBaseST):
    _dict = {}
    obj_type = 'instance_ip'

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interface = None
        self.update(obj)
    # end __init

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(fq_name=self.name)
        self.address = self.obj.get_instance_ip_address()
        self.update_single_ref('virtual_machine_interface', self.obj)
    # end update

    def delete_obj(self):
        self.update_single_ref('virtual_machine_interface', {})
# end InstanceIpST


class FloatingIpST(DBBaseST):
    _dict = {}
    obj_type = 'floating_ip'

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interface = None
        self.update(obj)
    # end __init

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(fq_name=self.name)
        self.address = self.obj.get_floating_ip_address()
        self.update_single_ref('virtual_machine_interface', self.obj)
    # end update

    def delete_obj(self):
        self.update_single_ref('virtual_machine_interface', {})
# end FloatingIpST


class VirtualMachineST(DBBaseST):
    _dict = {}
    obj_type = 'virtual_machine'

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interfaces = set()
        self.service_instance = None
        self.update(obj)
        self.uuid = self.obj.uuid
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(fq_name=self.name)
        self.update_multiple_refs('virtual_machine_interface', self.obj)
        self.update_single_ref('service_instance', self.obj)
    # end update

    def delete_obj(self):
        self.update_multiple_refs('virtual_machine_interface', {})
        self.update_single_ref('service_instance', {})
    # end delete_obj

    def add_interface(self, interface):
        self.virtual_machine_interfaces.add(interface)
    # end add_interface

    def delete_interface(self, interface):
        self.virtual_machine_interfaces.discard(interface)
    # end delete_interface

    def get_service_mode(self):
        if self.service_instance is None:
            return None
        si_obj = ServiceInstanceST.get(self.service_instance)
        if si_obj is None:
            self._logger.error("service instance %s not found"
                               % self.service_instance)
            return None
        return si_obj.get_service_mode()
    # end get_service_mode
# end VirtualMachineST


class LogicalRouterST(DBBaseST):
    _dict = {}
    obj_type = 'logical_router'

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interfaces = set()
        self.virtual_networks = set()
        self.rt_list = set()
        self.obj = obj or self.read_vnc_obj(fq_name=name)
        rt_ref = self.obj.get_route_target_refs()
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
                GlobalSystemConfigST.get_autonomous_system(), rtgt_num)
            rtgt_obj = RouteTargetST.locate(rt_key)
            self.obj.set_route_target(rtgt_obj.obj)
            self._vnc_lib.logical_router_update(self.obj)

        if old_rt_key:
            RouteTargetST.delete(old_rt_key)
        self.route_target = rt_key

        self.update(self.obj)
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(uuid=self.obj.uuid)
        self.update_multiple_refs('virtual_machine_interface', self.obj)
        self.update_virtual_networks()
        rt_list = self.obj.get_configured_route_target_list() or RouteTargetList()
        self.set_route_target_list(rt_list)
    # end update

    def delete_obj(self):
        self.update_multiple_refs('virtual_machine_interface', {})
        self.update_virtual_networks()
        rtgt_num = int(self.route_target.split(':')[-1])
        self._cassandra.free_route_target_by_number(rtgt_num)
        RouteTargetST.delete(self.route_target)
    # end delete_obj

    def update_virtual_networks(self):
        vn_set = set()
        for vmi in self.virtual_machine_interfaces:
            vmi_obj = VirtualMachineInterfaceST.get(vmi)
            if vmi_obj is not None:
                vn_set.add(vmi_obj.virtual_network)
        if vn_set == self.virtual_networks:
            return
        self.set_virtual_networks(vn_set)
    # end update_virtual_netwokrs

    def add_interface(self, intf_name):
        self.virtual_machine_interfaces.add(intf_name)
        self.update_virtual_networks()
    # end add_interface

    def delete_interface(self, intf_name):
        self.virtual_machine_interfaces.discard(intf_name)
        self.update_virtual_networks()
    # end delete_interface

    def set_virtual_networks(self, vn_set):
        for vn in self.virtual_networks - vn_set:
            vn_obj = VirtualNetworkST.get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                ri_obj.update_route_target_list(rt_add=set(),
                                                rt_del=self.rt_list|set([self.route_target]))
        for vn in vn_set - self.virtual_networks:
            vn_obj = VirtualNetworkST.get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                ri_obj.update_route_target_list(rt_add=self.rt_list|set([self.route_target]))
        self.virtual_networks = vn_set
    # end set_virtual_networks

    def update_autonomous_system(self, asn):
        old_rt = self.route_target
        rtgt_num = int(old_rt.split(':')[-1])
        rt_key = "target:%s:%d" % (asn, rtgt_num)
        rtgt_obj = RouteTargetST.locate(rt_key)
        try:
            obj = self.read_vnc_obj(fq_name=self.name)
            obj.set_route_target(rtgt_obj.obj)
            self._vnc_lib.logical_router_update(obj)
        except NoIdError:
            self._logger.error(
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

    def set_route_target_list(self, rt_list):
        old_rt_list = self.rt_list
        self.rt_list = set(rt_list.get_route_target())
        rt_add = self.rt_list - old_rt_list
        rt_del = old_rt_list - self.rt_list

        for vn in self.virtual_networks:
            vn_obj = VirtualNetworkST.get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                ri_obj.update_route_target_list(rt_del=rt_del,
                                                rt_add=rt_add)
    # end set_route_target_list

# end LogicalRouterST


class ServiceInstanceST(DBBaseST):
    _dict = {}
    obj_type = 'service_instance'

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machines = set()
        self.service_template = None
        self.auto_policy = False
        self.left_vn_str = None
        self.right_vn_str = None
        self.update(obj)
        self.network_policys = NetworkPolicyST.get_by_service_instance(self.name)
        self.route_tables = RouteTableST.get_by_service_instance(self.name)
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(fq_name=self.name)
        self.update_multiple_refs('virtual_machine', self.obj)
        st_refs = self.obj.get_service_template_refs()
        if st_refs:
            self.service_template = ':'.join(st_refs[0]['to'])
        props = self.obj.get_service_instance_properties()
        if props:
            self.add_properties(props)
    # end update

    def get_virtual_networks(self, si_props):
        left_vn = None
        right_vn = None

        st_refs = self.obj.get_service_template_refs()
        uuid = st_refs[0]['uuid']
        st_obj = DBBaseST().read_vnc_obj(uuid, obj_type='service_template')
        st_props = st_obj.get_service_template_properties()

        st_if_list = st_props.get_interface_type()
        si_if_list = si_props.get_interface_list()
        for (st_if, si_if) in zip(st_if_list, si_if_list):
            if st_if.get_service_interface_type() == 'left':
                left_vn = si_if.get_virtual_network()
            elif st_if.get_service_interface_type() == 'right':
                right_vn = si_if.get_virtual_network()

        if left_vn == "":
            parent_str = self.obj.get_parent_fq_name_str()
            left_vn = parent_str + ':' + svc_info.get_left_vn_name()
        if right_vn == "":
            parent_str = self.obj.get_parent_fq_name_str()
            right_vn = parent_str + ':' + svc_info.get_right_vn_name()

        return left_vn, right_vn
    # end get_si_vns

    def add_properties(self, props):
        if not props.auto_policy:
            return self.delete_properties()
        self.auto_policy = True
        self.left_vn_str, self.right_vn_str = self.get_virtual_networks(props)
        if (not self.left_vn_str or not self.right_vn_str):
            self._logger.error(
                "%s: route table next hop service instance must "
                "have left and right virtual networks", self.name)
            return self.delete_properties()

        policy_name = "_internal_" + self.name
        addr1 = AddressType(virtual_network=self.left_vn_str)
        addr2 = AddressType(virtual_network=self.right_vn_str)
        action_list = ActionListType(apply_service=[self.name])

        prule = PolicyRuleType(direction="<>", protocol="any",
                               src_addresses=[addr1], dst_addresses=[addr2],
                               src_ports=[PortType()], dst_ports=[PortType()],
                               action_list=action_list)
        pentry = PolicyEntriesType([prule])
        policy_obj = NetworkPolicy(policy_name, network_policy_entries=pentry)
        policy = NetworkPolicyST.locate(policy_name, policy_obj)
        policy.virtual_networks = set([self.left_vn_str, self.right_vn_str])

        policy.set_internal()
        networks = set()
        vn1 = VirtualNetworkST.get(self.left_vn_str)
        if vn1:
            vn1.add_policy(policy_name)
            networks.add(left_vn_str)
        vn2 = VirtualNetworkST.get(self.right_vn_str)
        if vn2:
            vn2.add_policy(policy_name)
            networks.add(right_vn_str)
        return networks
    # add_properties

    def delete_properties(self):
        networks = set()
        policy_name = '_internal_' + self.name
        policy = NetworkPolicyST.get(policy_name)
        if policy is None:
            return networks
        for vn_name in policy.virtual_networks:
            vn = VirtualNetworkST.get(vn_name)
            if vn is None:
                continue
            del vn.network_policys[policy_name]
            networks.add(vn_name)
        # end for vn_name
        NetworkPolicyST.delete(policy_name)
        return networks
    # end delete_properties

    def delete_obj(self):
        self.update_multiple_refs('virtual_machine', {})
        self.delete_properties()
    # end delete_obj

    def get_service_mode(self):
        st_name = self.service_template
        if st_name is None:
            self._logger.error("service template is None for service instance "
                               + self.name)
            return None
        try:
            st_obj = self.read_vnc_obj(fq_name=st_name,
                                       obj_type='service_template')
        except NoIdError:
            self._logger.error("NoIdError while reading service template "
                               + st_name)
            return None
        smode = st_obj.get_service_template_properties().get_service_mode()
        return smode or 'transparent'
    # end get_service_mode
# end ServiceInstanceST

