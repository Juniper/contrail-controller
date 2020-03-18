#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
"""
This file contains config data model for schema transformer
"""

import gevent.monkey
gevent.monkey.patch_all()

import sys
reload(sys)
sys.setdefaultencoding('UTF8')

import copy
import uuid

import itertools
import cfgm_common as common
from netaddr import IPNetwork, IPAddress
from cfgm_common.exceptions import NoIdError, RefsExistError, BadRequest
from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import ResourceExhaustionError
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

_PROTO_STR_TO_NUM_IPV4 = {
    'icmp6': '58',
    'icmp': '1',
    'tcp': '6',
    'udp': '17',
    'any': 'any',
}
_PROTO_STR_TO_NUM_IPV6 = {
    'icmp6': '58',
    'icmp': '58',
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
                "Error while creating acl %s for %s: %s" %
                (name, obj.get_fq_name_str(), str(e)))
        return None
    else:
        if entries is None:
            try:
                DBBaseST._vnc_lib.access_control_list_delete(id=acl_obj.uuid)
            except NoIdError:
                pass
            return None

        entries_hash = hash(entries)
        # if entries did not change, just return the object
        if acl_obj.get_access_control_list_hash() == entries_hash:
            return acl_obj

        # Set new value of entries on the ACL
        acl_obj.set_access_control_list_entries(entries)
        acl_obj.set_access_control_list_hash(entries_hash)
        try:
            DBBaseST._vnc_lib.access_control_list_update(acl_obj)
        except HttpError as he:
            DBBaseST._logger.error(
                "HTTP error while updating acl %s for %s: %d, %s" %
                (name, obj.get_fq_name_str(), he.status_code, he.content))
        except NoIdError:
            DBBaseST._logger.error("NoIdError while updating acl %s for %s" %
                                   (name, obj.get_fq_name_str()))
    return acl_obj
# end _access_control_list_update


class DBBaseST(DBBase):
    obj_type = __name__
    _indexed_by_name = True
    ref_fields = []
    prop_fields = []

    def update(self, obj=None):
        return self.update_vnc_obj(obj)

    def delete_obj(self):
        for ref_field in self.ref_fields:
            self.update_refs(ref_field, {})

    def evaluate(self, **kwargs):
        #Implement in the derived class
        pass

    @classmethod
    def reinit(cls):
        for obj in cls.list_vnc_obj():
            try:
                cls.locate(obj.get_fq_name_str(), obj)
            except Exception as e:
                cls._logger.error("Error in reinit for %s %s: %s" % (
                    cls.obj_type, obj.get_fq_name_str(), str(e)))
    # end reinit

    def handle_st_object_req(self):
        st_obj = sandesh.StObject(object_type=self.obj_type,
                                  object_fq_name=self.name)
        try:
            st_obj.object_uuid = self.obj.uuid
        except AttributeError:
            pass
        st_obj.obj_refs = []
        for field in self.ref_fields:
            if self._get_sandesh_ref_list(field):
                st_obj.obj_refs.append(self._get_sandesh_ref_list(field))

        st_obj.properties = [sandesh.PropList(field, str(getattr(self, field)))
                             for field in self.prop_fields
                             if hasattr(self, field)]
        return st_obj

    def _get_sandesh_ref_list(self, ref_type):
        try:
            ref = getattr(self, ref_type)
            refs = [ref] if ref else []
        except AttributeError:
            try:
                refs = getattr(self, ref_type + 's')
                if isinstance(refs, dict):
                    refs = refs.keys()
            except AttributeError:
                return
        return sandesh.RefList(ref_type, refs)
# end DBBaseST


class GlobalSystemConfigST(DBBaseST):
    _dict = {}
    obj_type = 'global_system_config'
    _autonomous_system = 0
    ibgp_auto_mesh = None
    prop_fields = ['autonomous_system', 'ibgp_auto_mesh', 'bgpaas_parameters']

    @classmethod
    def reinit(cls):
        for gsc in cls.list_vnc_obj():
            try:
                cls.locate(gsc.get_fq_name_str(), gsc)
            except Exception as e:
                cls._logger.error("Error in reinit for %s %s: %s" % (
                    cls.obj_type, gsc.get_fq_name_str(), str(e)))
    # end reinit

    def __init__(self, name, obj):
        self.name = name
        self.uuid = obj.uuid
        self.update(obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'autonomous_system' in changed:
            self.update_autonomous_system(self.obj.autonomous_system)
        if 'bgpaas_parameters' in changed:
            self.update_bgpaas_parameters(self.obj.bgpaas_parameters)
        return changed
    # end update

    def update_bgpaas_parameters(self, bgpaas_params):
        if bgpaas_params:
            new_range = {'start': bgpaas_params.port_start,
                         'end': bgpaas_params.port_end}
            self._cassandra._bgpaas_port_allocator.reallocate([new_range])

    @classmethod
    def get_autonomous_system(cls):
        return cls._autonomous_system
    # end get_autonomous_system

    @classmethod
    def get_ibgp_auto_mesh(cls):
        if cls.ibgp_auto_mesh is None:
            return True
        return cls.ibgp_auto_mesh
    # end get_ibgp_auto_mesh

    @classmethod
    def update_autonomous_system(cls, new_asn):
        if int(new_asn) == cls._autonomous_system:
            return False
        # From the global route target list, pick ones with the
        # changed ASN, and update the routing instances' referred
        # by the route target
        for route_tgt in RouteTargetST.values():
            _, asn, target = route_tgt.obj.get_fq_name()[0].split(':')
            if int(asn) != cls.get_autonomous_system():
                continue
            if int(target) < common.BGP_RTGT_MIN_ID:
                continue

            new_rtgt_name = "target:%s:%s" % (new_asn, target)
            new_rtgt_obj = RouteTargetST.locate(new_rtgt_name)
            old_rtgt_name = "target:%d:%s" % (cls._autonomous_system, target)
            old_rtgt_obj = RouteTarget(old_rtgt_name)

            for ri_ref in route_tgt.obj.get_routing_instance_back_refs() or []:
                rt_inst = RoutingInstanceST.get(':'.join(ri_ref['to']))
                if rt_inst:
                    ri = rt_inst.obj
                else:
                    continue
                inst_tgt_data = InstanceTargetType()
                ri.del_route_target(old_rtgt_obj)
                ri.add_route_target(new_rtgt_obj.obj, inst_tgt_data)
                cls._vnc_lib.routing_instance_update(ri)
                # Also, update the static_routes, if any, in the routing
                # instance with the new route target
                static_route_entries = ri.get_static_route_entries()
                if static_route_entries is None:
                    continue
                for static_route in static_route_entries.get_route() or []:
                    if old_rtgt_name in static_route.route_target:
                        static_route.route_target.remove(old_rtgt_name)
                        static_route.route_target.append(new_rtgt_name)
                    ri.set_static_route_entries(static_route_entries)
                    cls._vnc_lib.routing_instance_update(ri)

            # Updating the logical router referred by the route target with
            # new route target.
            for router_ref in route_tgt.obj.get_logical_router_back_refs() or []:
                lr = LogicalRouterST.get(':'.join(router_ref['to']))
                if lr:
                    logical_router = lr.obj
                else:
                    continue
                logical_router.del_route_target(old_rtgt_obj)
                logical_router.add_route_target(new_rtgt_obj.obj)
                cls._vnc_lib.logical_router_update(logical_router)

            RouteTargetST.delete_vnc_obj(old_rtgt_obj.get_fq_name()[0])

        cls._autonomous_system = int(new_asn)
        return True
    # end update_autonomous_system

    def evaluate(self, **kwargs):
        for router in BgpRouterST.values():
            router.update_global_asn(self._autonomous_system)
            router.update_peering()
        # end for router
        for router in LogicalRouterST.values():
            router.update_autonomous_system(self._autonomous_system)
        # end for router
        self.update_bgpaas_parameters(self.obj.bgpaas_parameters)
    # end evaluate
# end GlobalSystemConfigST


# a struct to store attributes related to Virtual Networks needed by
# schema transformer


class VirtualNetworkST(DBBaseST):
    _dict = {}
    obj_type = 'virtual_network'
    ref_fields = ['network_policy', 'virtual_machine_interface', 'route_table',
                  'bgpvpn', 'network_ipam']
    prop_fields = ['virtual_network_properties', 'route_target_list',
                   'multi_policy_service_chains_enabled']

    def me(self, name):
        return name in (self.name, 'any')

    def __init__(self, name, obj=None, acl_dict=None):
        self.name = name
        self.network_policys = OrderedDict()
        self.virtual_machine_interfaces = set()
        self.connections = set()
        self.routing_instances = set()
        self.acl = None
        self.dynamic_acl = None
        self.acl_rule_count = 0
        self.multi_policy_service_chains_enabled = None
        self.update_vnc_obj(obj)
        self.uuid = self.obj.uuid
        for acl in self.obj.get_access_control_lists() or []:
            if acl_dict:
                acl_obj = acl_dict[acl['uuid']]
            else:
                acl_obj = self.read_vnc_obj(
                    acl['uuid'], obj_type='access_control_list',
                    fields=['access_control_list_hash'])
            if acl_obj.name == self.obj.name:
                self.acl = acl_obj
            elif acl_obj.name == 'dynamic':
                self.dynamic_acl = acl_obj

        self.ipams = {}
        self.get_route_target_lists(self.obj)
        for rt in itertools.chain(self.rt_list, self.import_rt_list,
                                  self.export_rt_list):
            RouteTargetST.locate(rt)
        self._route_target = None
        self.route_tables = set()
        self.service_chains = {}
        prop = self.obj.get_virtual_network_properties(
        ) or VirtualNetworkType()
        self.allow_transit = prop.allow_transit
        # TODO(ethuleau): We keep the virtual network and security group ID
        #                 allocation in schema and in the vnc API for one
        #                 release overlap to prevent any upgrade issue. So the
        #                 following code need to be remove in release (3.2 + 1)
        nid = self.obj.get_virtual_network_network_id()
        if nid is None:
            nid = prop.network_id or self._cassandra.alloc_vn_id(name) + 1
            self.obj.set_virtual_network_network_id(nid)
            self._vnc_lib.virtual_network_update(self.obj)
        if self.obj.get_fq_name() == common.IP_FABRIC_VN_FQ_NAME:
            default_ri_fq_name = common.IP_FABRIC_RI_FQ_NAME
        elif self.obj.get_fq_name() == common.LINK_LOCAL_VN_FQ_NAME:
            default_ri_fq_name = common.LINK_LOCAL_RI_FQ_NAME
        else:
            default_ri_fq_name = self.obj.fq_name + [self.obj.name]
        self._default_ri_name = ':'.join(default_ri_fq_name)
        self.multi_policy_service_chains_status_changed = False
        self.update(self.obj)
        self.update_multiple_refs('virtual_machine_interface', self.obj)
    # end __init__

    def get_routes(self):
        routes = set()
        for rt in self.route_tables:
            rt_obj = RouteTableST.get(rt)
            if rt_obj:
                routes |= set(rt_obj.routes)
        return routes
    # end

    def update(self, obj=None):
        ret = self.update_vnc_obj(obj)

        old_policies = set(self.network_policys.keys())
        self.network_policys = OrderedDict()
        for policy_ref in self.obj.get_network_policy_refs() or []:
            self.add_policy(':'.join(policy_ref['to']), policy_ref.get('attr'))

        for policy_name in NetworkPolicyST.get_internal_policies(self.name):
            self.add_policy(policy_name)
        for policy_name in old_policies - set(self.network_policys.keys()):
            policy = NetworkPolicyST.get(policy_name)
            if policy:
                policy.virtual_networks.discard(self.name)

        for policy_name in set(self.network_policys.keys()) - old_policies:
            policy = NetworkPolicyST.get(policy_name)
            if policy:
                policy.virtual_networks.add(self.name)

        old_ipams = self.ipams
        self.ipams = {}
        for ipam_ref in self.obj.get_network_ipam_refs() or []:
            subnet = ipam_ref['attr']
            ipam_fq_name = ':'.join(ipam_ref['to'])
            if self.ipams.get(ipam_fq_name) != subnet:
                self.ipams[ipam_fq_name] = subnet
        if self.ipams != old_ipams:
            self.update_ipams()
        prop = self.obj.get_virtual_network_properties(
        ) or VirtualNetworkType()
        self.set_properties(prop)
        self.set_route_target_list(self.obj)
        self.multi_policy_service_chains_status_changed = (
            'multi_policy_service_chains_enabled' in ret)
        return ret
    # end update

    def _update_primary_ri_to_service_ri_connection(self, sc, si_name,
                                                    multi_policy_enabled):
            primary_ri = self.get_primary_routing_instance()
            service_ri_name = self.get_service_name(sc.name, si_name)
            service_ri = RoutingInstanceST.get(service_ri_name)
            if service_ri is None:
                return
            if (multi_policy_enabled and
                    service_ri_name in primary_ri.connections):
                primary_ri.delete_connection(service_ri)
                # add primary ri's route target to service ri
                rt_obj = RouteTargetST.get(self.get_route_target())
                service_ri.obj.add_route_target(rt_obj.obj,
                                                InstanceTargetType('import'))
                self._vnc_lib.routing_instance_update(service_ri.obj)
            elif (not multi_policy_enabled and
                    service_ri_name not in primary_ri.connections):
                primary_ri.add_connection(service_ri)
                # delete primary ri's route target from service ri
                rt_obj = RouteTargetST.get(self.get_route_target())
                service_ri.obj.del_route_target(rt_obj.obj)
                self._vnc_lib.routing_instance_update(service_ri.obj)
    # end _update_primary_ri_to_service_ri_connection

    def check_multi_policy_service_chain_status(self):
        if not self.multi_policy_service_chains_status_changed:
            return
        self.multi_policy_service_chains_status_changed = False
        for sc_list in self.service_chains.values():
            for sc in sc_list:
                if sc is None or not sc.created:
                    continue
                if sc.left_vn == self.name:
                    si_name = sc.service_list[0]
                    other_vn_name = sc.right_vn
                    other_si_name = sc.service_list[-1]
                elif sc.right_vn == self.name:
                    si_name = sc.service_list[-1]
                    other_vn_name = sc.left_vn
                    other_si_name = sc.service_list[0]
                else:
                    continue
                other_vn = VirtualNetworkST.get(other_vn_name)
                if not other_vn:
                    continue
                multi_policy_enabled = (
                    self.multi_policy_service_chains_enabled and
                    other_vn.multi_policy_service_chains_enabled)
                self._update_primary_ri_to_service_ri_connection(
                    sc, si_name, multi_policy_enabled)
                other_vn._update_primary_ri_to_service_ri_connection(
                    sc, other_si_name, multi_policy_enabled)
    # end check_multi_policy_service_chain_status

    def delete_inactive_service_chains(self, old_scs, new_scs=None):

        # Delete the service chains that are no longer active
        for remote_vn_name in old_scs:
            # Get the Remote VNs in this VN's service chain and
            # get a list of the remote service chains in the remote
            # VNs which has this VNs name.
            remote_vn = VirtualNetworkST.get(remote_vn_name)
            if remote_vn is None:
                remote_service_chain_list = []
            else:
                remote_service_chain_list = remote_vn.service_chains.get(
                    self.name)

            # Get a list of this VN's service chains which has a
            # remote VN name as one of its service endpoints.
            # Case 1: If the Service Chain is present in the updated
            #         SC list (if any), then dont do anythnig.
            # Case 2: If the SC is not present in the updated SC
            #         list (if any), but present in the remote VN
            #         SC list, then invalidate it and mark it for
            #         deletion.
            # Case 3: If the SC is not present in the updated SC
            #         list (if any) and not in the remote VN SC list,
            #         then delete it permanentely since there is no
            #         VNs that are referring to this SC.
            service_chain_list = old_scs[remote_vn_name]
            for service_chain in service_chain_list or []:
                if new_scs and service_chain in (new_scs.get(remote_vn_name) or
                                                 []):
                    continue
                if service_chain in (remote_service_chain_list or []):
                    service_chain.destroy()
                else:
                    service_chain.delete()

    def delete_obj(self):
        for policy_name in self.network_policys:
            policy = NetworkPolicyST.get(policy_name)
            if not policy:
                continue
            policy.virtual_networks.discard(self.name)
        self.update_multiple_refs('virtual_machine_interface', {})
        self.delete_inactive_service_chains(self.service_chains)
        for ri_name in self.routing_instances:
            ri = RoutingInstanceST.get(ri_name)
            # Don't delete default RI, API server will do and schema will
            # clean RT and its internal when it will receive the RI delete
            # notification. That prevents ST to fail to delete RT because RI
            # was not yet removed
            if ri is not None and not ri.is_default:
                ri.delete(ri_name, True)
        if self.acl:
            self._vnc_lib.access_control_list_delete(id=self.acl.uuid)
        if self.dynamic_acl:
            self._vnc_lib.access_control_list_delete(id=self.dynamic_acl.uuid)
        # TODO(ethuleau): We keep the virtual network and security group ID
        #                 allocation in schema and in the vnc API for one
        #                 release overlap to prevent any upgrade issue. So the
        #                 following code need to be remove in release (3.2 + 1)
        nid = self.obj.get_virtual_network_network_id()
        if nid is None:
            props = self.obj.get_virtual_network_properties()
            if props:
                nid = props.network_id
        if nid:
            self._cassandra.free_vn_id(nid - 1)

        self.update_multiple_refs('route_table', {})
        self.uve_send(deleted=True)
    # end delete_obj

    def add_policy(self, policy_name, attrib=None):
        # Add a policy ref to the vn. Keep it sorted by sequence number
        if attrib is None:
            attrib = VirtualNetworkPolicyType(SequenceType(sys.maxint,
                                                           sys.maxint))
        if attrib.sequence is None:
            self._logger.error("Cannot assign policy %s to %s: sequence "
                               "number is not available" % (policy_name,
                                                            self.name))
            return False

        if self.network_policys.get(policy_name) == attrib:
            return False
        self.network_policys[policy_name] = attrib
        self.network_policys = OrderedDict(sorted(self.network_policys.items(),
                                           key=lambda t:(t[1].sequence.major,
                                                         t[1].sequence.minor)))
        return True
    # end add_policy

    def get_primary_routing_instance(self):
        return RoutingInstanceST.get(self._default_ri_name)
    # end get_primary_routing_instance

    def add_connection(self, vn_name):
        if vn_name == "any":
            self.connections.add("*")
            # Need to do special processing for "*"
        elif vn_name != self.name:
            self.connections.add(vn_name)
    # end add_connection

    def add_ri_connection(self, vn2_name, primary_ri=None):
        vn2 = VirtualNetworkST.get(vn2_name)
        if not vn2:
            # connection published on receiving <vn2>
            return

        if self.name in vn2.expand_connections():
            ri1 = primary_ri or self.get_primary_routing_instance()
            ri2 = vn2.get_primary_routing_instance()
            if ri1 and ri2:
                ri1.add_connection(ri2)
        vn2.uve_send()
    # end add_ri_connection

    def delete_ri_connection(self, vn2_name):
        vn2 = VirtualNetworkST.get(vn2_name)
        if vn2 is None:
            return
        ri1 = self.get_primary_routing_instance()
        ri2 = vn2.get_primary_routing_instance()
        if ri1 and ri2:
            ri1.delete_connection(ri2)
        vn2.uve_send()
    # end delete_ri_connection

    def add_service_chain(self, svn, dvn, proto, prule):
        if self.me(dvn):
            remote_vn = svn
        elif self.me(svn):
            remote_vn = dvn
        else:
            return {}
        if self.name == remote_vn:
            self._logger.error("Service chain source and dest vn are same: %s"
                               % self.name)
            return None
        if remote_vn == 'any':
            remote_vns = self.get_vns_in_project()
        else:
            remote_vns = [remote_vn]
        service_chain_ris = {}
        for remote_vn in remote_vns:
            if remote_vn not in VirtualNetworkST:
                self._logger.error("Network %s not found while apply service "
                                   "chain to network %s" % (remote_vn,
                                                            self.name))
                continue
            services = prule.action_list.apply_service
            service_chain_list = self.service_chains.setdefault(remote_vn, [])
            if self.me(dvn):
                service_chain = ServiceChain.find_or_create(
                    remote_vn, self.name, prule.direction, prule.src_ports,
                    prule.dst_ports, proto, services)
                service_chain_ris[remote_vn] = (
                    None, self.get_service_name(service_chain.name,
                                                services[-1]))
            else:
                service_chain = ServiceChain.find_or_create(
                    self.name, remote_vn, prule.direction, prule.src_ports,
                    prule.dst_ports, proto, services)
                service_chain_ris[remote_vn] = (
                    self.get_service_name(service_chain.name, services[0]),
                    None)

            if service_chain not in service_chain_list:
                service_chain_list.append(service_chain)
        return service_chain_ris
    # end add_service_chain

    def get_vns_in_project(self):
        # return a set of all virtual networks with the same parent as self
        return set((k) for k, v in self._dict.items()
                   if (self.name != v.name and
                       self.obj.get_parent_fq_name() ==
                       v.obj.get_parent_fq_name()))
    # end get_vns_in_project

    def allocate_service_chain_ip(self, sc_fq_name):
        sc_name = sc_fq_name.split(':')[-1]
        v4_address, v6_address = self._cassandra.get_service_chain_ip(sc_name)
        if v4_address or v6_address:
            return v4_address, v6_address
        try:
            v4_address = self._vnc_lib.virtual_network_ip_alloc(
                self.obj, count=1, family='v4')[0]
        except (NoIdError, RefsExistError) as e:
            self._logger.error(
                "Error while allocating ipv4 in network %s: %s" % (self.name,
                                                                   str(e)))
        try:
            v6_address = self._vnc_lib.virtual_network_ip_alloc(
                self.obj, count=1, family='v6')[0]
        except (NoIdError, RefsExistError) as e:
            self._logger.error(
                "Error while allocating ipv6 in network %s: %s" % (self.name,
                                                                   str(e)))
        if v4_address is None and v6_address is None:
            return None, None
        self._cassandra.add_service_chain_ip(sc_name, v4_address, v6_address)
        return v4_address, v6_address
    # end allocate_service_chain_ip

    def free_service_chain_ip(self, sc_fq_name):
        sc_name = sc_fq_name.split(':')[-1]
        v4_address, v6_address = self._cassandra.get_service_chain_ip(sc_name)
        ip_addresses = []
        if v4_address:
            ip_addresses.append(v4_address)
        if v6_address:
            ip_addresses.append(v6_address)
        if not ip_addresses:
            return
        self._cassandra.remove_service_chain_ip(sc_name)
        try:
            self._vnc_lib.virtual_network_ip_free(self.obj, ip_addresses)
        except NoIdError:
            pass
    # end free_service_chain_ip

    def get_route_target(self):
        if self._route_target is None:
            ri = self.get_primary_routing_instance()
            if ri is None:
                return
            ri_name = ri.get_fq_name_str()
            self._route_target = self._cassandra.get_route_target(ri_name)
        rtgt_name = "target:%s:%d" % (
                    GlobalSystemConfigST.get_autonomous_system(),
                    self._route_target)
        return rtgt_name
    # end get_route_target

    def set_route_target(self, rtgt_name):
        _, asn, target = rtgt_name.split(':')
        if int(asn) != GlobalSystemConfigST.get_autonomous_system():
            return
        self._route_target = int(target)
    # end set_route_target

    @staticmethod
    def _ri_needs_external_rt(vn_name, ri_name):
        sc_id = RoutingInstanceST._get_service_id_from_ri(ri_name)
        if sc_id is None:
            return False
        sc = ServiceChain.get(sc_id)
        if sc is None:
            return False
        if vn_name == sc.left_vn:
            return ri_name.endswith(sc.service_list[0].replace(':', '_'))
        elif vn_name == sc.right_vn:
            return ri_name.endswith(sc.service_list[-1].replace(':', '_'))
        return False
    # end _ri_needs_external_rt

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
            return False
        self.allow_transit = properties.allow_transit
        ret = False
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
                ri = RoutingInstanceST.get(ri_name)
                if not ri:
                    continue
                if self.allow_transit:
                    # if the network is now a transit network, add the VN's
                    # route target to all service RIs
                    ri.update_route_target_list(
                        rt_add_export=[self.get_route_target()])
                else:
                    # if the network is not a transit network any more, then we
                    # need to delete the route target from service RIs
                    ri.update_route_target_list(rt_del=[self.get_route_target()])
                ret = True
        return ret
    # end set_properties

    def get_route_target_lists(self, obj):
        rt_list = obj.get_route_target_list()
        if rt_list:
            self.rt_list = set(rt_list.get_route_target())
        else:
            self.rt_list = set()
        rt_list = obj.get_import_route_target_list()
        if rt_list:
            self.import_rt_list = set(rt_list.get_route_target())
        else:
            self.import_rt_list = set()
        rt_list = obj.get_export_route_target_list()
        if rt_list:
            self.export_rt_list = set(rt_list.get_route_target())
        else:
            self.export_rt_list = set()

        # if any RT exists in both import and export, just add it to rt_list
        self.rt_list |= self.import_rt_list & self.export_rt_list
        # if any RT exists in rt_list, remove it from import/export lists
        self.import_rt_list -= self.rt_list
        self.export_rt_list -= self.rt_list
    # end get_route_target_lists

    def set_route_target_list(self, obj):
        ri = self.get_primary_routing_instance()
        old_rt_list = set(self.rt_list)
        old_import_rt_list = set(self.import_rt_list)
        old_export_rt_list = set(self.export_rt_list)
        self.get_route_target_lists(obj)

        rt_add = self.rt_list - old_rt_list
        rt_add_export = self.export_rt_list - old_export_rt_list
        rt_add_import = self.import_rt_list - old_import_rt_list
        rt_del = ((old_rt_list - self.rt_list) |
                  (old_export_rt_list - self.export_rt_list) |
                  (old_import_rt_list - self.import_rt_list))
        if not (rt_add or rt_add_export or rt_add_import or rt_del):
            return False
        for rt in itertools.chain(rt_add, rt_add_export, rt_add_import):
            RouteTargetST.locate(rt)
        if ri:
            ri.update_route_target_list(rt_add=rt_add,
                                        rt_add_import=rt_add_import,
                                        rt_add_export=rt_add_export,
                                        rt_del=rt_del)
        for ri_name in self.routing_instances:
            if self._ri_needs_external_rt(self.name, ri_name):
                service_ri = RoutingInstanceST.get(ri_name)
                service_ri.update_route_target_list(rt_add_export=rt_add,
                                                    rt_del=rt_del)
        for route in self.get_routes():
            prefix = route.prefix
            nexthop = route.next_hop
            (left_ri, _) = self._get_routing_instance_from_route(nexthop)
            if left_ri is None:
                continue
            left_ri.update_route_target_list(rt_add_import=rt_add,
                                             rt_del=rt_del)
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

        for rt in rt_del - (rt_add | rt_add_export | rt_add_import):
            try:
                RouteTargetST.delete_vnc_obj(rt)
            except RefsExistError:
                # if other routing instances are referring to this target,
                # it will be deleted when those instances are deleted
                pass
        return True
    # end set_route_target_list

    # next-hop in a route contains fq-name of a service instance, which must
    # be an auto policy instance. This function will get the left vn for that
    # service instance and get the primary and service routing instances
    def _get_routing_instance_from_route(self, next_hop):
        si = ServiceInstanceST.get(next_hop)
        if si is None:
            self._logger.error("Cannot find service instance %s" % next_hop)
            return (None, None)
        if not si.left_vn_str:
            self._logger.error("%s: route table next hop service instance "
                               "must have left virtual network" % self.name)
            return (None, None)
        left_vn = VirtualNetworkST.get(si.left_vn_str)
        if left_vn is None:
            self._logger.error("Virtual network %s not present"
                               % si.left_vn_str)
            return (None, None)
        return (left_vn.get_primary_routing_instance(), si)
    # end _get_routing_instance_from_route

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

        vn_trace.total_acl_rules = self.acl_rule_count
        for ri_name in self.routing_instances:
            vn_trace.routing_instance_list.append(ri_name)
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
        vn_msg = UveVirtualNetworkConfigTrace(data=vn_trace,
                                              sandesh=self._sandesh)
        vn_msg.send(sandesh=self._sandesh)
    # end uve_send

    def get_service_name(self, sc_name, si_name):
        name = "service-%s-%s" % (sc_name, si_name)
        return "%s:%s" % (self.obj.get_fq_name_str(), name.replace(':', '_'))
    # end get_service_name

    @staticmethod
    def get_analyzer_vn_and_ip(analyzer_name):
        vn_analyzer = None
        ip_analyzer = None
        si = ServiceInstanceST.get(analyzer_name)
        if si is None:
            return (None, None)
        vm_analyzer = list(si.virtual_machines)
        pt_list = list(si.port_tuples)
        if not vm_analyzer and not pt_list:
            return (None, None)
        if pt_list:
            vm_pt = PortTupleST.get(pt_list[0])
        else:
            vm_pt = VirtualMachineST.get(vm_analyzer[0])
        if vm_pt is None:
            return (None, None)
        vmis = vm_pt.virtual_machine_interfaces
        for vmi_name in vmis:
            vmi = VirtualMachineInterfaceST.get(vmi_name)
            if vmi and vmi.is_left():
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
                self._logger.debug("Mirror: adding connection: %s to %s" %
                                   (self.name, vn_analyzer))
                self.add_connection(vn_analyzer)
                vn_obj = VirtualNetworkST.get(vn_analyzer)
                if vn_obj:
                    vn_obj.add_connection(self.name)
            else:
                self._logger.error("Mirror: %s: no analyzer vn for %s" %
                                   (self.name, analyzer_name))
        except NoIdError:
            return
    # end process_analyzer

    @staticmethod
    def protocol_policy_to_acl(pproto, ethertype):
        # convert policy proto input(in str) to acl proto (num)
        if pproto is None:
            return 'any'
        if pproto.isdigit():
            return pproto
        if ethertype == 'IPv6':
            return _PROTO_STR_TO_NUM_IPV6.get(pproto.lower())
        else: # IPv4
            return _PROTO_STR_TO_NUM_IPV4.get(pproto.lower())
    # end protocol_policy_to_acl

    def address_list_policy_to_acl(self, addr):
        if addr.network_policy:
            pol = NetworkPolicyST.get(addr.network_policy)
            if not pol:
                self._logger.error(
                    "Policy %s not found while applying policy "
                    "to network %s" % (addr.network_policy, self.name))
                return []
            return [AddressType(virtual_network=x)
                    for x in pol.virtual_networks]
        else:
            return [addr]
    # end address_list_policy_to_acl

    def policy_to_acl_rule(self, prule, dynamic):
        result_acl_rule_list = AclRuleListST(dynamic=dynamic)
        saddr_list = prule.src_addresses
        sp_list = prule.src_ports
        daddr_list = prule.dst_addresses
        dp_list = prule.dst_ports
        rule_uuid = prule.get_rule_uuid()

        arule_proto = self.protocol_policy_to_acl(prule.protocol, prule.ethertype)
        if arule_proto is None:
            self._logger.error("Unknown protocol %s" % prule.protocol)
            return result_acl_rule_list

        if prule.action_list is None:
            self._logger.error("No action specified in policy rule "
                               "attached to %s. Ignored." % self.name)
            return result_acl_rule_list

        for saddr in saddr_list:
            saddr_match = copy.deepcopy(saddr)
            svn = saddr.virtual_network
            spol = saddr.network_policy
            s_cidr = saddr.subnet
            s_cidr_list = saddr.subnet_list
            if svn == "local":
                svn = self.name
                saddr_match.virtual_network = self.name
            for daddr in daddr_list:
                daddr_match = copy.deepcopy(daddr)
                dvn = daddr.virtual_network
                dpol = daddr.network_policy
                d_cidr = daddr.subnet
                d_cidr_list = daddr.subnet_list
                if dvn == "local":
                    dvn = self.name
                    daddr_match.virtual_network = self.name
                if self.me(dvn):
                    remote_network_name = svn
                elif self.me(svn):
                    remote_network_name = dvn
                elif not dvn and dpol:
                    dp_obj = NetworkPolicyST.get(dpol)
                    if self.name in dp_obj.virtual_networks:
                        remote_network_name = svn
                        daddr_match.network_policy = None
                        daddr_match.virtual_network = self.name
                    else:
                        self._logger.debug(
                            "network policy rule attached to %s has src = %s,"
                            " dst = %s. Ignored." % (self.name,
                                                     svn or spol,
                                                     dvn or dpol))
                        continue

                elif not svn and spol:
                    sp_obj = NetworkPolicyST.get(spol)
                    if self.name in sp_obj.virtual_networks:
                        remote_network_name = dvn
                        saddr_match.network_policy = None
                        saddr_match.virtual_network = self.name
                    else:
                        self._logger.debug(
                            "network policy rule attached to %s has src = %s,"
                            " dst = %s. Ignored." % (self.name,
                                                     svn or spol,
                                                     dvn or dpol))
                        continue
                elif (not svn and not dvn and not spol and not dpol and
                      (s_cidr or s_cidr_list) and (d_cidr or d_cidr_list)):
                    if prule.action_list.apply_service:
                        self._logger.debug(
                            "service chains not allowed in cidr only rules "
                            "network %s, src = %s, dst = %s. Ignored." %
                            (self.name, s_cidr, d_cidr))
                        continue
                else:
                    self._logger.debug("network policy rule attached to %s"
                                       " has svn = %s, dvn = %s. Ignored." %
                                       (self.name, svn, dvn))
                    continue

                action = prule.action_list
                if action.mirror_to and action.mirror_to.analyzer_name:
                    if self.me(svn) or self.me(dvn):
                        self.process_analyzer(action)

                sa_list = self.address_list_policy_to_acl(saddr_match)
                da_list = self.address_list_policy_to_acl(daddr_match)

                service_ris = {}
                service_list = prule.action_list.apply_service
                if service_list:
                    service_ris = self.add_service_chain(svn, dvn, arule_proto,
                                                         prule)
                    if not service_ris:
                        continue
                    if self.me(svn):
                        da_list = [AddressType(
                                       virtual_network=x,
                                       subnet=daddr_match.subnet,
                                       subnet_list=daddr_match.subnet_list)
                                   for x in service_ris]
                    elif self.me(dvn):
                        sa_list = [AddressType(
                                       virtual_network=x,
                                       subnet=saddr_match.subnet,
                                       subnet_list=saddr_match.subnet_list)
                                   for x in service_ris]

                for sp, dp, sa, da in itertools.product(sp_list, dp_list,
                                                        sa_list, da_list):
                    service_ri = None
                    if self.me(sa.virtual_network):
                        service_ri = service_ris.get(da.virtual_network, [None])[0]
                    elif self.me(da.virtual_network):
                        service_ri = service_ris.get(sa.virtual_network,
                                                     [None, None])[1]
                    acl = self.add_acl_rule(
                        sa, sp, da, dp, arule_proto, rule_uuid,
                        prule.action_list, prule.direction, service_ri)
                    result_acl_rule_list.append(acl)
                    acl_direction_comp = self._manager._args.acl_direction_comp
                    if ((prule.direction == "<>") and (sa != da or sp != dp)
                         and (not acl_direction_comp)):
                        acl = self.add_acl_rule(
                            da, dp, sa, sp, arule_proto, rule_uuid,
                            prule.action_list, prule.direction, service_ri)
                        result_acl_rule_list.append(acl)

                # end for sp, dp
            # end for daddr
        # end for saddr
        return result_acl_rule_list
    # end policy_to_acl_rule

    def add_acl_rule(self, sa, sp, da, dp, proto, rule_uuid, action, direction,
                     service_ri = None):
        action_list = copy.deepcopy(action)
        action_list.set_assign_routing_instance(service_ri)
        match = MatchConditionType(proto, sa, sp, da, dp)
        acl_direction_comp = self._manager._args.acl_direction_comp
        if acl_direction_comp:
            acl = AclRuleType(match, action_list, rule_uuid, direction)
        else:
            acl = AclRuleType(match, action_list, rule_uuid)
        return acl

    def update_pnf_presence(self):
        has_pnf = False
        for ri_name in self.routing_instances:
            if ri_name == self._default_ri_name:
                continue
            ri = RoutingInstanceST.get(ri_name)
            if ri is None:
                continue
            if ri.obj.get_routing_instance_has_pnf():
                has_pnf = True
                break
        default_ri = RoutingInstanceST.get(self._default_ri_name)
        if (default_ri and
                default_ri.obj.get_routing_instance_has_pnf() != has_pnf):
            default_ri.obj.set_routing_instance_has_pnf(has_pnf)
            self._vnc_lib.routing_instance_update(default_ri.obj)
    # end update_pnf_presence

    def evaluate(self, **kwargs):
        self.timer = kwargs.get('timer')
        old_virtual_network_connections = self.expand_connections()
        old_service_chains = self.service_chains
        self.connections = set()
        self.service_chains = {}
        self.acl_rule_count = 0

        static_acl_entries = None
        dynamic_acl_entries = None
        for policy_name in self.network_policys:
            timer = self.network_policys[policy_name].get_timer()
            if timer is None:
                if static_acl_entries is None:
                    static_acl_entries = AclEntriesType(dynamic=False)
                acl_entries = static_acl_entries
                dynamic = False
            else:
                if dynamic_acl_entries is None:
                    dynamic_acl_entries = AclEntriesType(dynamic=True)
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
                    if self.me(match.dst_address.virtual_network):
                        connected_network = match.src_address.virtual_network
                    elif self.me(match.src_address.virtual_network):
                        connected_network = match.dst_address.virtual_network
                    if action.apply_service:
                        # if a service was applied, the ACL should have a
                        # pass action, and we should not make a connection
                        # between the routing instances
                        action.simple_action = "pass"
                        action.apply_service = []
                        if self.multi_policy_service_chains_enabled:
                            other_vn = VirtualNetworkST.get(connected_network)
                            if not other_vn:
                                continue
                            if other_vn.multi_policy_service_chains_enabled:
                                self.add_connection(connected_network)
                        continue

                    if connected_network and action.simple_action:
                        self.add_connection(connected_network)

                    if self.timer:
                        self.timer.timed_yield(is_evaluate_yield=True)
                # end for acl_rule_list
                if self.timer:
                    self.timer.timed_yield(is_evaluate_yield=True)
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

            if self._manager._args.logical_routers_enabled:
                for rule in static_acl_entries.get_acl_rule():
                    src_address = copy.deepcopy(rule.match_condition.src_address)
                    dst_address = copy.deepcopy(rule.match_condition.dst_address)
                    if src_address.virtual_network:
                        src_address.subnet = None
                        src_address.subnet_list = []
                    if dst_address.virtual_network:
                        dst_address.subnet = None
                        dst_address.subnet_list = []

                    acl = self.add_acl_rule(src_address, PortType(), dst_address,
                                       PortType(), "any", rule.get_rule_uuid(),
                                       ActionListType("deny"), rule.direction)

                    acl_list.append(acl)

                    acl_direction_comp = self._manager._args.acl_direction_comp
                    if ((rule.direction == "<>") and (src_address != dst_address)
                         and (not acl_direction_comp)):
                        acl = self.add_acl_rule(src_address, PortType(), dst_address,
                                           PortType(), "any", rule.get_rule_uuid(),
                                           ActionListType("deny"), rule.direction)
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
            else:
                # Create any-vn to any-vn deny
                match = MatchConditionType(
                    "any", AddressType(virtual_network="any"), PortType(),
                    AddressType(virtual_network="any"), PortType())
                action = ActionListType("deny")
                acl = AclRuleType(match, action)
                acl_list.append(acl)
                acl_list.update_acl_entries(static_acl_entries)
            self.acl_rule_count = len(static_acl_entries.get_acl_rule())


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

        self.delete_inactive_service_chains(old_service_chains, self.service_chains)

        primary_ri = self.get_primary_routing_instance()
        if primary_ri:
            primary_ri.update_static_routes()
        self.update_pnf_presence()
        self.check_multi_policy_service_chain_status()
        for ri_name in self.routing_instances:
            ri = RoutingInstanceST.get(ri_name)
            if ri:
                ri.update_routing_policy_and_aggregates()
        self.uve_send()
    # end evaluate

    def get_prefixes(self, ip_version):
        prefixes = []
        for ipam in self.ipams.values():
            for ipam_subnet in ipam.ipam_subnets:
                prefix = '%s/%d' % (ipam_subnet.subnet.ip_prefix,
                                    ipam_subnet.subnet.ip_prefix_len)
                network = IPNetwork(prefix)
                if network.version == ip_version:
                    prefixes.append(prefix)
        return prefixes
    # end get_prefixes

    def handle_st_object_req(self):
        resp = super(VirtualNetworkST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('routing_instance'),
            sandesh.RefList('virtual_network', self.connections),
            self._get_sandesh_ref_list('service_chain')
        ])
        resp.properties.extend([
            sandesh.PropList('route_target', self.get_route_target()),
            sandesh.PropList('network_id',
                             str(self.obj.get_virtual_network_network_id())),
            sandesh.PropList('rt_list', ', '.join(self.rt_list)),
            sandesh.PropList('import_rt_list',
                             ', '.join(self.import_rt_list)),
            sandesh.PropList('export_rt_list',
                             ', '.join(self.export_rt_list)),
        ])
        return resp
    # end handle_st_object_req

    def get_gateway(self, address):
        """Returns the defualt gateway of the network
        to which the 'address' belongs
        """
        for ipam in self.ipams.values():
            for ipam_subnet in ipam.ipam_subnets:
                network = IPNetwork('%s/%s' % (ipam_subnet.subnet.ip_prefix,
                                    ipam_subnet.subnet.ip_prefix_len))
                if address in network:
                    return ipam_subnet.default_gateway
    # end get_gateway
# end class VirtualNetworkST


class RouteTargetST(DBBaseST):
    _dict = {}
    obj_type = 'route_target'

    @classmethod
    def reinit(cls):
        for obj in cls.list_vnc_obj():
            try:
                if (obj.get_routing_instance_back_refs() or
                        obj.get_logical_router_back_refs()):
                    cls.locate(obj.get_fq_name_str(), obj)
                else:
                    cls.delete_vnc_obj(obj.get_fq_name_str())
            except Exception as e:
                cls._logger.error("Error in reinit for %s %s: %s" % (
                    cls.obj_type, obj.get_fq_name_str(), str(e)))
        for ri, val in cls._cassandra._rt_cf.get_range():
            rt = val['rtgt_num']
            asn = GlobalSystemConfigST.get_autonomous_system()
            rt_key = "target:%s:%s" % (asn, rt)
            if rt_key not in cls:
                cls._cassandra.free_route_target(ri)
    # end reinit

    def __init__(self, rt_key, obj=None):
        self.name = rt_key
        try:
            self.obj = obj or self.read_vnc_obj(fq_name=[rt_key])
        except NoIdError:
            self.obj = RouteTarget(rt_key)
            self._vnc_lib.route_target_create(self.obj)
    # end __init__

    def update(self, obj=None):
        return False

    @classmethod
    def delete_vnc_obj(cls, key):
        try:
            cls._vnc_lib.route_target_delete(fq_name=[key])
        except NoIdError:
            pass
        cls._dict.pop(key, None)
    # end delete_vnc_obj
# end RoutTargetST

# a struct to store attributes related to Network Policy needed by schema
# transformer


class NetworkPolicyST(DBBaseST):
    _dict = {}
    obj_type = 'network_policy'
    prop_fields = ['network_policy_entries']
    _internal_policies = set()
    _service_instances = {}
    _network_policys = {}

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_networks = set()
        self.service_instances = set()
        self.internal = False
        self.rules = []
        self.has_subnet_only_rules = True
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

    def skip_evaluate(self, from_type):
        if from_type == 'virtual_network' and self.has_subnet_only_rules == True:
            return True
        return False
    # end skip_evaluate

    def update_subnet_only_rules(self):
        for rule in self.rules:
            for address in itertools.chain(rule.src_addresses,
                                           rule.dst_addresses):
                if (address.virtual_network not in (None, 'local') or
                    address.network_policy):
                    self.has_subnet_only_rules = False
    # end update_subnet_only_rules

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
        ret = self.update_vnc_obj(obj)
        if ret:
            self.add_rules(self.obj.get_network_policy_entries())
        return ret
    # end update

    def add_rules(self, entries):
        if entries is None:
            if not self.rules:
                return False
            self.rules = []
        else:
            if self.rules == entries.policy_rule:
                return False
            self.rules = entries.policy_rule
        np_set = set()
        si_set = set()
        for prule in self.rules:
            if prule.action_list is None:
                continue
            if (prule.action_list.mirror_to and
                prule.action_list.mirror_to.analyzer_name):
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
        self.update_subnet_only_rules()
        return True
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
    # end delete_obj

    def handle_st_object_req(self):
        resp = super(NetworkPolicyST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('virtual_network'),
            self._get_sandesh_ref_list('service_instance'),
            self._get_sandesh_ref_list('network_policy'),
            sandesh.RefList('referred_policy', self.referred_policies)
        ]
        resp.properties = [
            sandesh.PropList('rule', str(rule)) for rule in self.rules
        ]
        return resp
    # end handle_st_object_req
# end class NetworkPolicyST


class RouteTableST(DBBaseST):
    _dict = {}
    obj_type = 'route_table'
    prop_fields = ['routes']

    _service_instances = {}

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_networks = set()
        self.logical_routers = set()
        self.service_instances = set()
        self.routes = []
        self.update(obj)
        self.update_multiple_refs('virtual_network', self.obj)
        self.update_multiple_refs('logical_router', self.obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'routes' in changed:
            if self.routes is None:
                self.routes = []
            else:
                self.routes = self.routes.get_route() or []
            si_set = set([route.next_hop for route in self.routes
                          if route.next_hop_type != 'ip-address'])
            self.update_service_instances(si_set)
        return changed
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
        self.update_multiple_refs('logical_router', {})

    def handle_st_object_req(self):
        resp = super(RouteTableST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('virtual_network'),
            self._get_sandesh_ref_list('service_instance'),
            self._get_sandesh_ref_list('logical_router'),
        ]
        resp.properties = [
            sandesh.PropList('route', str(route)) for route in self.routes
        ]
        return resp
    # end handle_st_object_req
# end RouteTableST

# a struct to store attributes related to Security Group needed by schema
# transformer


class SecurityGroupST(DBBaseST):
    _dict = {}
    obj_type = 'security_group'
    prop_fields = ['security_group_entries', 'configured_security_group_id']
    _sg_dict = {}

    def __init__(self, name, obj=None, acl_dict=None):
        def _get_acl(uuid):
            if acl_dict:
                return acl_dict[uuid]
            return self.read_vnc_obj(uuid, obj_type='access_control_list')

        self.name = name
        self.obj = obj or self.read_vnc_obj(fq_name=name)
        self.uuid = self.obj.uuid
        self.configured_security_group_id = None
        self.sg_id = None
        self.ingress_acl = None
        self.egress_acl = None
        self.referred_sgs = set()
        self.security_group_entries = None
        acls = self.obj.get_access_control_lists()
        for acl in acls or []:
            if acl['to'][-1] == 'egress-access-control-list':
                self.egress_acl = _get_acl(acl['uuid'])
            elif acl['to'][-1] == 'ingress-access-control-list':
                self.ingress_acl = _get_acl(acl['uuid'])
            else:
                self._vnc_lib.access_control_list_delete(id=acl['uuid'])
        self.update(self.obj)
        self.security_groups = SecurityGroupST._sg_dict.get(name, set())
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        # TODO(ethuleau): We keep the virtual network and security group ID
        #                 allocation in schema and in the vnc API for one
        #                 release overlap to prevent any upgrade issue. So the
        #                 following code need to be remove in release (3.2 + 1)
        if 'configured_security_group_id' in changed:
            self.set_configured_security_group_id()
        if changed:
            self.process_referred_sgs()
        return changed
    # end update

    def process_referred_sgs(self):
        if self.security_group_entries:
            prules = self.security_group_entries.get_policy_rule() or []
        else:
            prules = []

        sg_refer_set = set()
        for prule in prules:
            for addr in prule.src_addresses + prule.dst_addresses:
                if addr.security_group:
                    if addr.security_group not in ['local', self.name, 'any']:
                        sg_refer_set.add(addr.security_group)
        # end for prule

        for sg_name in self.referred_sgs - sg_refer_set:
            sg_set = SecurityGroupST._sg_dict.get(sg_name)
            if sg_set is None:
                continue
            sg_set.discard(self.name)
            if not sg_set:
                del self._sg_dict[sg_name]
            sg = SecurityGroupST.get(sg_name)
            if sg:
                sg.security_groups = sg_set

        for sg_name in sg_refer_set - self.referred_sgs:
            sg_set = SecurityGroupST._sg_dict.setdefault(sg_name, set())
            sg_set.add(self.name)
            sg = SecurityGroupST.get(sg_name)
            if sg:
                sg.security_groups = sg_set
        self.referred_sgs = sg_refer_set
    # end process_referred_sgs

    # TODO(ethuleau): We keep the virtual network and security group ID
    #                 allocation in schema and in the vnc API for one
    #                 release overlap to prevent any upgrade issue. So the
    #                 following code need to be remove in release (3.2 + 1)
    def set_configured_security_group_id(self):
        sg_id = self.obj.get_security_group_id()
        config_id = self.configured_security_group_id or 0
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
        self.sg_id = self.obj.get_security_group_id()
        return True
    # end set_configured_security_group_id

    def delete_obj(self):
        if self.ingress_acl:
            self._vnc_lib.access_control_list_delete(id=self.ingress_acl.uuid)
        if self.egress_acl:
            self._vnc_lib.access_control_list_delete(id=self.egress_acl.uuid)
        # TODO(ethuleau): We keep the virtual network and security group ID
        #                 allocation in schema and in the vnc API for one
        #                 release overlap to prevent any upgrade issue. So the
        #                 following code need to be remove in release (3.2 + 1)
        sg_id = self.obj.get_security_group_id()
        if sg_id is not None and not self.configured_security_group_id:
            if sg_id < SGID_MIN_ALLOC:
                self._cassandra.free_sg_id(sg_id)
            else:
                self._cassandra.free_sg_id(sg_id-SGID_MIN_ALLOC)
        self.security_group_entries = None
        self.process_referred_sgs()
    # end delete_obj

    def evaluate(self, **kwargs):
        self.update_policy_entries()

    def update_policy_entries(self):
        ingress_acl_entries = AclEntriesType()
        egress_acl_entries = AclEntriesType()

        if self.security_group_entries:
            prules = self.security_group_entries.get_policy_rule() or []
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
            return True
        if addr.security_group in ['local', self.name]:
            addr.security_group = str(self.obj.get_security_group_id())
        elif addr.security_group == 'any':
            addr.security_group = '-1'
        elif addr.security_group in self._dict:
            addr.security_group = str(self._dict[
                addr.security_group].obj.get_security_group_id())
        else:
            return False
        return True
    # end _convert_security_group_name_to_id

    @staticmethod
    def protocol_policy_to_acl(pproto, ethertype):
        # convert policy proto input(in str) to acl proto (num)
        if pproto is None:
            return None
        if pproto.isdigit():
            return pproto
        if ethertype == 'IPv6':
            return _PROTO_STR_TO_NUM_IPV6.get(pproto.lower())
        else: # IPv4
            return _PROTO_STR_TO_NUM_IPV4.get(pproto.lower())

    def policy_to_acl_rule(self, prule):
        ingress_acl_rule_list = []
        egress_acl_rule_list = []
        rule_uuid = prule.get_rule_uuid()

        ethertype = prule.ethertype
        arule_proto = self.protocol_policy_to_acl(prule.protocol, ethertype)
        if arule_proto is None:
            # TODO log unknown protocol
            return (ingress_acl_rule_list, egress_acl_rule_list)

        acl_rule_list = None
        for saddr in prule.src_addresses:
            saddr_match = copy.deepcopy(saddr)
            if not self._convert_security_group_name_to_id(saddr_match):
                continue
            if saddr.security_group == 'local':
                saddr_match.security_group = None
                acl_rule_list = egress_acl_rule_list

            # If no src port is specified, assume 0-65535
            for sp in prule.src_ports or [PortType()]:
                for daddr in prule.dst_addresses:
                    daddr_match = copy.deepcopy(daddr)
                    if not self._convert_security_group_name_to_id(daddr_match):
                        continue
                    if daddr.security_group == 'local':
                        daddr_match.security_group = None
                        acl_rule_list = ingress_acl_rule_list
                    if acl_rule_list is None:
                        self._logger.error("SG rule must have either source "
                                           "or destination as 'local': " +
                                           self.name)
                        continue

                    # If no dst port is specified, assume 0-65535
                    for dp in prule.dst_ports or [PortType()]:
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

    def handle_st_object_req(self):
        resp = super(SecurityGroupST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('security_group'),
            sandesh.RefList('referred_security_group', self.referred_sgs)
        ]
        resp.properties.extend([
            sandesh.PropList('sg_id', str(self.sg_id)),
        ] + [sandesh.PropList('rule', str(rule))
             for rule in self.security_group_entries.get_policy_rule() or []])
        return resp
    # end handle_st_object_req
# end class SecurityGroupST

# a struct to store attributes related to Routing Instance needed by
# schema transformer


class RoutingInstanceST(DBBaseST):
    _dict = {}
    obj_type = 'routing_instance'

    def __init__(self, name, obj=None):
        self.name = name
        self.obj = obj or self.read_vnc_obj(fq_name=name)
        self.stale_route_targets = []
        self.service_chain = self._get_service_id_from_ri(self.name)
        self.connections = set()
        self.virtual_network = None
        self.is_default = self.obj.get_routing_instance_is_default()
        self.add_to_parent(self.obj)
        self.route_target = None
        self.routing_policys = {}
        self.route_aggregates = set()
        self.service_chain_info = self.obj.get_service_chain_information()
        self.v6_service_chain_info = self.obj.get_ipv6_service_chain_information()
        if self.obj.get_parent_fq_name() in [common.IP_FABRIC_VN_FQ_NAME,
                                             common.LINK_LOCAL_VN_FQ_NAME]:
            return
        self.locate_route_target()
        for ri_ref in self.obj.get_routing_instance_refs() or []:
            conn_fq_name = ':'.join(ri_ref['to'])
            if conn_fq_name != 'ERROR':
                self.connections.add(conn_fq_name)
            else:
                self._logger.debug("Invalid connection detected in RI " + name)
                # Remove the connection ref in the API server as well.
                try:
                    self._vnc_lib.ref_update('routing-instance', self.obj.uuid,
                                             'routing-instance', ri_ref['uuid'],
                                             None, 'DELETE')
                except NoIdError as e:
                    self._logger.debug("Ref not found in DB for RI " + name)
                    self._logger.debug(e)
        if self.is_default:
            vn = VirtualNetworkST.get(self.virtual_network)
            if vn is None:
                return

            for network in vn.expand_connections():
                vn.add_ri_connection(network, self)
            # if primary RI is connected to another primary RI, we need to
            # also create connection between the VNs
            for connection in self.connections:
                remote_ri_fq_name = connection.split(':')
                if remote_ri_fq_name[-1] == remote_ri_fq_name[-2]:
                    vn.connections.add(':'.join(remote_ri_fq_name[0:-1] ))
        vmi_refs = self.obj.get_virtual_machine_interface_back_refs() or []
        self.virtual_machine_interfaces = set([':'.join(ref['to'])
                                              for ref in vmi_refs])
        self.update_multiple_refs('route_aggregate', self.obj)
        for ref in self.obj.get_routing_policy_back_refs() or []:
            rp_name = ':'.join(ref['to'])
            self.routing_policys[rp_name] = ref['attr']['sequence']
        if self.is_default:
            self.update_static_routes()
    # end __init__

    def update(self, obj=None):
        # Nothing to do
        return False

    @classmethod
    def create(cls, fq_name, vn_obj, has_pnf=False):
        try:
            name = fq_name.split(':')[-1]
            ri_obj = RoutingInstance(name, parent_obj=vn_obj.obj,
                                     routing_instance_has_pnf=has_pnf)
            cls._vnc_lib.routing_instance_create(ri_obj)
            return ri_obj
        except RefsExistError:
            return cls.read_vnc_obj(fq_name=fq_name)
    # end create

    @staticmethod
    def _get_service_id_from_ri(name):
        ri_name = name.split(':')[-1]
        if not ri_name.startswith('service-'):
            return None
        return ri_name[8:44]
    # end _get_service_id_from_ri

    def update_routing_policy_and_aggregates(self):
        if not self.service_chain:
            return
        sc = ServiceChain.get(self.service_chain)
        if sc is None:
            return
        for si_name in sc.service_list:
            if not self.name.endswith(si_name.replace(':', '_')):
                continue
            si = ServiceInstanceST.get(si_name)
            if si is None:
                return
            if sc.left_vn == self.virtual_network:
                rp_dict = dict((rp, attr.get_left_sequence())
                               for rp, attr in si.routing_policys.items()
                               if attr.get_left_sequence())
                ra_set = set(ra for ra, if_type in si.route_aggregates.items()
                             if if_type == 'left')
            elif sc.right_vn == self.virtual_network:
                rp_dict = dict((rp, attr.get_right_sequence())
                               for rp, attr in si.routing_policys.items()
                               if attr.get_right_sequence())
                ra_set = set(ra for ra, if_type in si.route_aggregates.items()
                             if if_type == 'right')
            else:
                break
            for rp_name in self.routing_policys:
                if rp_name not in rp_dict:
                    rp = RoutingPolicyST.get(rp_name)
                    if rp:
                        rp.delete_routing_instance(self)
            for rp_name, seq in rp_dict.items():
                if (rp_name not in self.routing_policys or
                    seq != self.routing_policys[rp_name]):
                    rp = RoutingPolicyST.get(rp_name)
                    if rp:
                        rp.add_routing_instance(self, seq)
            self.routing_policys = rp_dict
            for ra_name in self.route_aggregates - ra_set:
                ra = RouteAggregateST.get(ra_name)
                if ra:
                    ra.delete_routing_instance(self)
            for ra_name in ra_set - self.route_aggregates:
                ra = RouteAggregateST.get(ra_name)
                if ra:
                    ra.add_routing_instance(self)
            self.route_aggregates = ra_set
    # end update_routing_policy_and_aggregates

    def import_default_ri_route_target_to_service_ri(self):
        update_ri = False
        if not self.service_chain:
            return update_ri
        sc = ServiceChain.get(self.service_chain)
        if sc is None or not sc.created:
            return update_ri
        left_vn = VirtualNetworkST.get(sc.left_vn)
        right_vn = VirtualNetworkST.get(sc.right_vn)
        if left_vn is None or right_vn is None:
            self._logger.debug("left or right vn not found for RI " + self.name)
            return update_ri

        multi_policy_enabled = (
            left_vn.multi_policy_service_chains_enabled and
            right_vn.multi_policy_service_chains_enabled)
        if not multi_policy_enabled:
            return update_ri
        vn = VirtualNetworkST.get(self.virtual_network)
        if sc.left_vn == vn.name:
            si_name = sc.service_list[0]
        elif sc.right_vn == vn.name:
            si_name = sc.service_list[-1]
        else:
            return update_ri
        service_ri_name = vn.get_service_name(sc.name, si_name)
        if service_ri_name == self.name:
            rt = vn.get_route_target()
            if rt not in self.stale_route_targets:
                rt_obj = RouteTargetST.get(rt)
                self.obj.add_route_target(rt_obj.obj,
                                          InstanceTargetType('import'))
                update_ri = True
            else:
                self.stale_route_targets.remove(rt)
        return update_ri
    # end import_default_ri_route_target_to_service_ri

    def locate_route_target(self):
        old_rtgt = self._cassandra.get_route_target(self.name)
        rtgt_num = self._cassandra.alloc_route_target(self.name)

        rt_key = "target:%s:%d" % (GlobalSystemConfigST.get_autonomous_system(),
                                   rtgt_num)
        rtgt_obj = RouteTargetST.locate(rt_key).obj
        if self.is_default:
            inst_tgt_data = InstanceTargetType()
        elif VirtualNetworkST._ri_needs_external_rt(self.virtual_network, self.name):
            inst_tgt_data = InstanceTargetType(import_export="export")
        else:
            inst_tgt_data = None

        vn = VirtualNetworkST.get(self.virtual_network)
        if vn is None:
            self._logger.error("Parent VN not found for RI: " + self.name)
            return

        try:
            if self.obj.parent_uuid != vn.obj.uuid:
                # Stale object. Delete it.
                self._vnc_lib.routing_instance_delete(id=self.obj.uuid)
                self.obj = None
            else:
                update_ri = False
                self.stale_route_targets = [':'.join(rt_ref['to'])
                        for rt_ref in self.obj.get_route_target_refs() or []]
                if rt_key not in self.stale_route_targets:
                    self.obj.add_route_target(rtgt_obj, InstanceTargetType())
                    update_ri = True
                else:
                    self.stale_route_targets.remove(rt_key)
                if inst_tgt_data:
                    for rt in vn.rt_list:
                        if rt not in self.stale_route_targets:
                            rtgt_obj = RouteTargetST.locate(rt)
                            self.obj.add_route_target(rtgt_obj.obj, inst_tgt_data)
                            update_ri = True
                        else:
                            self.stale_route_targets.remove(rt)
                    if self.is_default:
                        for rt in vn.export_rt_list:
                            if rt not in self.stale_route_targets:
                                rtgt_obj = RouteTargetST.locate(rt)
                                self.obj.add_route_target(
                                    rtgt_obj.obj, InstanceTargetType('export'))
                                update_ri = True
                            else:
                                self.stale_route_targets.remove(rt)
                        for rt in vn.import_rt_list:
                            if rt not in self.stale_route_targets:
                                rtgt_obj = RouteTargetST.locate(rt)
                                self.obj.add_route_target(
                                    rtgt_obj.obj, InstanceTargetType('import'))
                                update_ri = True
                            else:
                                self.stale_route_targets.remove(rt)
                    elif vn.allow_transit:
                        if vn.get_route_target() not in self.stale_route_targets:
                            rtgt_obj = RouteTarget(vn.get_route_target())
                            self.obj.add_route_target(rtgt_obj, inst_tgt_data)
                            update_ri = True
                        else:
                            self.stale_route_targets.remove(vn.get_route_target())
                update_ri |= self.import_default_ri_route_target_to_service_ri()
                if update_ri:
                    self._vnc_lib.routing_instance_update(self.obj)
        except NoIdError as e:
            self._logger.error(
                "Error while updating routing instance: " + str(e))
            raise
            return

        self.route_target = rt_key
        if self.is_default:
            vn.set_route_target(rt_key)

        if 0 < old_rtgt < common.BGP_RTGT_MIN_ID:
            rt_key = "target:%s:%d" % (
                GlobalSystemConfigST.get_autonomous_system(), old_rtgt)
            RouteTargetST.delete_vnc_obj(rt_key)
    # end locate_route_target

    def get_fq_name(self):
        return self.obj.get_fq_name()
    # end get_fq_name

    def get_fq_name_str(self):
        return self.obj.get_fq_name_str()
    # end get_fq_name_str

    def add_connection(self, ri2):
        self.connections.add(ri2.name)
        ri2.connections.add(self.name)

        conn_data = ConnectionType()
        self._vnc_lib.ref_update('routing-instance', self.obj.uuid,
                                 'routing-instance', ri2.obj.uuid,
                                 None, 'ADD', conn_data)
    # end add_connection

    def delete_connection(self, ri2):
        self.connections.discard(ri2.name)
        ri2.connections.discard(self.name)
        try:
            self._vnc_lib.ref_update('routing-instance', self.obj.uuid,
                                     'routing-instance', ri2.obj.uuid,
                                     None, 'DELETE')
        except NoIdError:
            return
    # end delete_connection

    def fill_service_info(self, service_info, ip_version, remote_vn,
                          service_instance, address, source_ri):
        if service_info is None:
            service_info = ServiceChainInfo()
        if service_instance:
            service_info.set_service_instance(service_instance)
        if address:
            service_info.set_routing_instance(
                remote_vn.get_primary_routing_instance().get_fq_name_str())
            service_info.set_service_chain_address(address)
        if source_ri:
            service_info.set_source_routing_instance(source_ri)
        prefixes = remote_vn.get_prefixes(ip_version)
        service_info.set_prefix(prefixes)
        if service_info.get_service_chain_address() is None:
            self._logger.error(
                "service chain ip address is None for: " + service_instance)
            return None
        return service_info
    # fill_service_info

    def add_service_info(self, remote_vn, service_instance=None,
                         v4_address=None, v6_address=None, source_ri=None):
        v4_info = self.obj.get_service_chain_information()
        v4_info = self.fill_service_info(v4_info, 4, remote_vn,
                                         service_instance, v4_address,
                                         source_ri)
        self.service_chain_info = v4_info
        self.obj.set_service_chain_information(v4_info)
        v6_info = self.obj.get_ipv6_service_chain_information()
        v6_info = self.fill_service_info(v6_info, 6, remote_vn,
                                         service_instance, v6_address,
                                         source_ri)
        self.v6_service_chain_info = v6_info
        self.obj.set_ipv6_service_chain_information(v6_info)
    # end add_service_info

    def update_route_target_list(self, rt_add=None, rt_add_import=None,
                                 rt_add_export=None, rt_del=None):
        update = False
        for rt in rt_del or []:
            if rt in self.stale_route_targets:
                self.stale_route_targets.remove(rt)
            rtgt_obj = RouteTarget(rt)
            self.obj.del_route_target(rtgt_obj)
            update = True
        for rt in rt_add or []:
            if rt not in self.stale_route_targets:
                rtgt_obj = RouteTargetST.locate(rt).obj
                inst_tgt_data = InstanceTargetType(import_export=None)
                self.obj.add_route_target(rtgt_obj, inst_tgt_data)
                update = True
            else:
                self.stale_route_targets.remove(rt)
        for rt in rt_add_import or []:
            if rt not in self.stale_route_targets:
                rtgt_obj = RouteTargetST.locate(rt).obj
                inst_tgt_data = InstanceTargetType(import_export='import')
                self.obj.add_route_target(rtgt_obj, inst_tgt_data)
                update = True
            else:
                self.stale_route_targets.remove(rt)
        for rt in rt_add_export or []:
            if rt not in self.stale_route_targets:
                rtgt_obj = RouteTargetST.locate(rt).obj
                inst_tgt_data = InstanceTargetType(import_export='export')
                self.obj.add_route_target(rtgt_obj, inst_tgt_data)
                update = True
            else:
                self.stale_route_targets.remove(rt)
        if update:
            try:
                self._vnc_lib.routing_instance_update(self.obj)
            except NoIdError:
                return
    # end update_route_target_list

    def update_static_routes(self):
        if not self.is_default:
            return
        old_static_routes = self.obj.get_static_route_entries()
        static_routes = StaticRouteEntriesType()
        old_route_target_list = set()
        if old_static_routes:
            for sr in old_static_routes.get_route():
                old_route_target_list |= set(sr.route_target or [])
        all_route_targets = set()
        si_set = ServiceInstanceST.get_vn_si_mapping(self.virtual_network)
        for si in si_set or []:
            mode = si.get_service_mode()
            if mode is None or mode != 'in-network-nat':
                self._logger.debug("service mode for %s-%s, skip" % (si.name, mode))
                continue
            route_tables = RouteTableST.get_by_service_instance(si.name)
            sc_address = {4: si.get_allocated_interface_ip("left", 4),
                          6: si.get_allocated_interface_ip("left", 6)}
            for route_table_name in route_tables:
                route_table = RouteTableST.get(route_table_name)
                if route_table is None or not route_table.routes:
                    self._logger.debug("route table/routes None for: " +
                                       route_table_name)
                    continue
                route_targets = set()
                for lr_name in route_table.logical_routers:
                    lr = LogicalRouterST.get(lr_name)
                    if lr is None:
                        self._logger.debug("lr is None for: " + lr_name)
                        continue
                    route_targets = route_targets | lr.rt_list | set([lr.route_target])
                if not route_targets:
                    self._logger.debug("route targets None for: " + route_table_name)
                    continue
                for route in route_table.routes:
                    if route.next_hop != si.name:
                        continue
                    version = IPNetwork(route.prefix).version
                    if not sc_address[version]:
                        continue
                    static_route = StaticRouteType(prefix=route.prefix,
                                                   next_hop=sc_address[version],
                                                   route_target=list(route_targets))
                    static_routes.add_route(static_route)
                    all_route_targets |= route_targets

        if old_route_target_list != all_route_targets and si_set is not None:
            self.update_route_target_list(
                    rt_add_import=all_route_targets - old_route_target_list,
                    rt_del=old_route_target_list - all_route_targets)

        #update static ip routes
        vn_obj = VirtualNetworkST.get(self.virtual_network)
        for rt_name in vn_obj.route_tables:
            route_table = RouteTableST.get(rt_name)
            if route_table is None or route_table.routes is None:
                continue
            for route in route_table.routes or []:
                if route.next_hop_type == "ip-address":
                    cattr = route.get_community_attributes()
                    communities = cattr.community_attribute if cattr else None
                    static_route = StaticRouteType(prefix=route.prefix,
                                                   next_hop=route.next_hop,
                                                   community=communities)
                    static_routes.add_route(static_route)
            # end for route
        # end for route_table
        if static_routes != old_static_routes:
            self.obj.set_static_route_entries(static_routes)
            try:
                self._vnc_lib.routing_instance_update(self.obj)
            except NoIdError:
                pass
    # end update_static_routes

    def delete_obj(self):
        for ri2_name in self.connections:
            ri2 = RoutingInstanceST.get(ri2_name)
            if ri2:
                ri2.connections.discard(self.name)

        rtgt_list = self.obj.get_route_target_refs()
        self._cassandra.free_route_target(self.name)

        service_chain = self.service_chain
        vn_obj = VirtualNetworkST.get(self.virtual_network)
        if vn_obj is not None and service_chain is not None:
            vn_obj.free_service_chain_ip(self.obj.name)

            uve = UveServiceChainData(name=service_chain, deleted=True)
            uve_msg = UveServiceChain(data=uve, sandesh=self._sandesh)
            uve_msg.send(sandesh=self._sandesh)

            for vmi_name in list(self.virtual_machine_interfaces):
                vmi = VirtualMachineInterfaceST.get(vmi_name)
                if vmi:
                    vm_pt_list = vmi.get_virtual_machine_or_port_tuple()
                    for vm_pt in vm_pt_list:
                        self._cassandra.free_service_chain_vlan(vm_pt.uuid,
                                                             service_chain)
                    vmi.delete_routing_instance(self)
            # end for vmi_name

        for rp_name in self.routing_policys:
            rp = RoutingPolicyST.get(rp_name)
            if rp:
                rp.delete_routing_instance(self)
            else:
                try:
                    rp_obj = RoutingPolicyST.read_vnc_obj(fq_name=rp_name)
                    if rp_obj:
                        rp_obj.del_routing_instance(self.obj)
                        self._vnc_lib.routing_policy_update(rp_obj)
                except NoIdError:
                    pass
        for ra_name in self.route_aggregates:
            ra = RouteAggregateST.get(ra_name)
            if ra:
                ra.delete_routing_instance(self)
            else:
                try:
                    ra_obj = RouteAggregateST.read_vnc_obj(fq_name=ra_name)
                    if ra_obj:
                        ra_obj.del_routing_instance(self.obj)
                        self._vnc_lib.route_aggregate_update(ra_obj)
                except NoIdError:
                    pass

        self.routing_policys = {}
        self.route_aggregates = set()
        bgpaas_server_name = self.obj.get_fq_name_str() + ':bgpaas-server'
        bgpaas_server = BgpRouterST.get(bgpaas_server_name)
        if bgpaas_server:
            try:
                self._vnc_lib.bgp_router_delete(id=bgpaas_server.obj.uuid)
            except NoIdError:
                pass
            BgpRouterST.delete(bgpaas_server_name)
        try:
            # The creation/deletion of the default virtual network routing
            # instance is handled by the vnc api now
            if not self.is_default:
                DBBaseST._vnc_lib.routing_instance_delete(id=self.obj.uuid)

        except NoIdError:
            pass

        for rtgt in rtgt_list or []:
            try:
                RouteTargetST.delete_vnc_obj(rtgt['to'][0])
            except RefsExistError:
                # if other routing instances are referring to this target,
                # it will be deleted when those instances are deleted
                pass
    # end delete_obj

    @classmethod
    def delete(cls, key, delete_vnc_obj=False):
        obj = cls.get(key)
        if obj is None:
            return
        if obj.is_default or delete_vnc_obj:
            obj.delete_obj()
            del cls._dict[key]

    def handle_st_object_req(self):
        resp = super(RoutingInstanceST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('virtual_network'),
            sandesh.RefList('routing_instance', self.connections),
        ]
        resp.properties = [
            sandesh.PropList('service_chain', self.service_chain),
            sandesh.PropList('is_default', str(self.is_default)),
            sandesh.PropList('route_target', self.route_target),
        ]
        return resp
    # end handle_st_object_req
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
            if not hasattr(chain, 'partially_created'):
                chain.partially_created = False
            if not hasattr(chain, 'si_info'):
                chain.si_info = None
            cls._dict[name] = chain
    # end init

    def __init__(self, name, left_vn, right_vn, direction, sp_list, dp_list,
                 protocol, services):
        self.name = name
        self.left_vn = left_vn
        self.right_vn = right_vn
        self.direction = direction
        self.sp_list = sp_list
        self.dp_list = dp_list
        self.service_list = list(services)
        self.si_info = None

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
        if self.service_list != other.service_list:
            return False
        return True
    # end __eq__

    @classmethod
    def find(cls, left_vn, right_vn, direction, sp_list, dp_list, protocol,
             service_list):
        for sc in ServiceChain.values():
            if (left_vn == sc.left_vn and
                right_vn == sc.right_vn and
                sp_list == sc.sp_list and
                dp_list == sc.dp_list and
                direction == sc.direction and
                protocol == sc.protocol and
                service_list == sc.service_list):
                    return sc
        # end for sc
        return None
    # end find

    @classmethod
    def find_or_create(cls, left_vn, right_vn, direction, sp_list, dp_list,
                       protocol, service_list):
        sc = cls.find(left_vn, right_vn, direction, sp_list, dp_list, protocol,
                      service_list)
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
            service_ri = RoutingInstanceST.get(service_name)
            if service_ri is None:
                continue
            service_ri.add_service_info(vn2, service)
            self._vnc_lib.routing_instance_update(service_ri.obj)
        # end for service
    # end update_ipams

    def log_error(self, msg):
        self.error_msg = msg
        self._logger.error('service chain %s: %s' % (self.name, msg))
    # end log_error

    def _get_vm_pt_info(self, vm_pt, mode):
        # From a VirtualMachineST or PortTupleST object, create a vm_info
        # dict to be used during service chain creation
        vm_info = {'vm_uuid': vm_pt.uuid}

        for interface_name in vm_pt.virtual_machine_interfaces:
            interface = VirtualMachineInterfaceST.get(interface_name)
            if not interface:
                continue
            if not (interface.is_left() or interface.is_right()):
                continue
            v4_addr = None
            v6_addr = None
            if mode != 'transparent':
                v4_addr = interface.get_any_instance_ip_address(4)
                v6_addr = interface.get_any_instance_ip_address(6)
                if v4_addr is None and v6_addr is None:
                    self.log_error("No ip address found for interface "
                                   + interface_name)
                    return None
            vmi_info = {'vmi': interface, 'v4-address': v4_addr,
                        'v6-address': v6_addr}
            vm_info[interface.service_interface_type] = vmi_info

        if 'left' not in vm_info:
            self.log_error('Left interface not found for %s' %
                           vm_pt.name)
            return None
        if ('right' not in vm_info and mode != 'in-network-nat' and
            self.direction == '<>'):
            self.log_error('Right interface not found for %s' %
                           vm_pt.name)
            return None
        return vm_info
    # end _get_vm_pt_info

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
                self.log_error("Service instance %s not found " % service)
                return None
            vm_list = si.virtual_machines
            pt_list = si.port_tuples
            if not vm_list and not pt_list:
                self.log_error("No vms/pts found for service instance " + service)
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
                vm_info = self._get_vm_pt_info(vm_obj, mode)
                if vm_info:
                    vm_info_list.append(vm_info)
            for pt in pt_list:
                pt_obj = PortTupleST.get(pt)
                if pt_obj is None:
                    self.log_error('virtual machine %s not found' % pt)
                    return None
                vm_info = self._get_vm_pt_info(pt_obj, mode)
                if vm_info:
                    vm_info_list.append(vm_info)
            # end for service_vm
            if not vm_info_list:
                return None
            virtualization_type = si.get_virtualization_type()
            ret_dict[service] = {'mode': mode, 'vm_list': vm_info_list,
                                 'virtualization_type': virtualization_type}
        return ret_dict
    # check_create

    def create(self):
        si_info = self.check_create()
        if self.created:
            if self.created_stale:
                self.uve_send()
                self.created_stale = False
            if si_info is None:
                # if previously created but no longer valid, then destroy
                self.destroy()
                return

            # If the VMIs associated with the SC has changed
            # after the SC is created, recreate the SC object.
            if self.si_info != None and si_info != self.si_info:
                self._create(si_info)
                self.si_info = si_info
                if self.partially_created:
                    self.destroy()
                    return
            return

        if si_info is None:
            return
        self._create(si_info)
        if self.partially_created:
            self.destroy()
            return
        self.si_info = si_info
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
        uve_msg = UveServiceChain(data=uve, sandesh=self._sandesh)
        uve_msg.send(sandesh=self._sandesh)
    # end uve_send

    def _create(self, si_info):
        self.partially_created = True
        vn1_obj = VirtualNetworkST.locate(self.left_vn)
        vn2_obj = VirtualNetworkST.locate(self.right_vn)
        if not vn1_obj or not vn2_obj:
            self.log_error("vn1_obj or vn2_obj is None")
            return

        multi_policy_enabled = (vn1_obj.multi_policy_service_chains_enabled and
                                vn2_obj.multi_policy_service_chains_enabled)
        service_ri2 = None
        if not multi_policy_enabled:
            service_ri2 = vn1_obj.get_primary_routing_instance()
            if service_ri2 is None:
                self.log_error("primary ri is None for " + self.left_vn)
                return
        first_node = True
        for service in self.service_list:
            service_name1 = vn1_obj.get_service_name(self.name, service)
            service_name2 = vn2_obj.get_service_name(self.name, service)
            has_pnf = (si_info[service]['virtualization_type'] == 'physical-device')
            ri_obj = RoutingInstanceST.create(service_name1, vn1_obj, has_pnf)
            service_ri1 = RoutingInstanceST.locate(service_name1, ri_obj)
            if service_ri1 is None:
                self.log_error("service_ri1 is None")
                return
            if service_ri2 is not None:
                service_ri2.add_connection(service_ri1)
            else:
                # add primary ri's target to service ri
                rt_obj = RouteTargetST.get(vn1_obj.get_route_target())
                service_ri1.obj.add_route_target(rt_obj.obj,
                                                 InstanceTargetType('import'))
                self._vnc_lib.routing_instance_update(service_ri1.obj)

            mode = si_info[service]['mode']
            nat_service = (mode == "in-network-nat")
            transparent = (mode not in ["in-network", "in-network-nat"])
            self._logger.info("service chain %s: creating %s chain" %
                              (self.name, mode))

            if not nat_service:
                ri_obj = RoutingInstanceST.create(service_name2, vn2_obj, has_pnf)
                service_ri2 = RoutingInstanceST.locate(service_name2, ri_obj)
                if service_ri2 is None:
                    self.log_error("service_ri2 is None")
                    return
            else:
                service_ri2 = None

            if first_node:
                first_node = False
                rt_list = set(vn1_obj.rt_list)
                if vn1_obj.allow_transit:
                    rt_list.add(vn1_obj.get_route_target())
                service_ri1.update_route_target_list(rt_add_export=rt_list)

            if transparent:
                v4_address, v6_address = vn1_obj.allocate_service_chain_ip(
                    service_name1)
                if v4_address is None and v6_address is None:
                    self.log_error('Cannot allocate service chain ip address')
                    return
                service_ri1.add_service_info(vn2_obj, service, v4_address,
                                             v6_address)
                if service_ri2 and self.direction == "<>":
                    service_ri2.add_service_info(vn1_obj, service, v4_address,
                                                 v6_address)

            for vm_info in si_info[service]['vm_list']:
                if transparent:
                    result = self.process_transparent_service(
                        vm_info, v4_address, v6_address, service_ri1,
                        service_ri2)
                else:
                    result = self.process_in_network_service(
                        vm_info, service, vn1_obj, vn2_obj, service_ri1,
                        service_ri2, nat_service)
                if not result:
                    return
            self._vnc_lib.routing_instance_update(service_ri1.obj)
            if service_ri2:
                self._vnc_lib.routing_instance_update(service_ri2.obj)

        if service_ri2:
            rt_list = set(vn2_obj.rt_list)
            if vn2_obj.allow_transit:
                rt_list.add(vn2_obj.get_route_target())
            service_ri2.update_route_target_list(rt_add_export=rt_list)

            if not multi_policy_enabled:
                service_ri2.add_connection(
                    vn2_obj.get_primary_routing_instance())
            else:
                # add primary ri's target to service ri
                rt_obj = RouteTargetST.get(vn2_obj.get_route_target())
                service_ri2.obj.add_route_target(rt_obj.obj,
                                                 InstanceTargetType('import'))
                self._vnc_lib.routing_instance_update(service_ri2.obj)

        self.created = True
        self.partially_created = False
        self.error_msg = None
        self._cassandra.add_service_chain_uuid(self.name, jsonpickle.encode(self))
    # end _create

    def add_pbf_rule(self, vmi, ri, v4_address, v6_address, vlan):
        if vmi.service_interface_type not in ["left", "right"]:
            return

        pbf = PolicyBasedForwardingRuleType(
            direction='both', vlan_tag=vlan, service_chain_address=v4_address,
            ipv6_service_chain_address=v6_address)

        if vmi.is_left():
            pbf.set_src_mac('02:00:00:00:00:01')
            pbf.set_dst_mac('02:00:00:00:00:02')
        else:
            pbf.set_src_mac('02:00:00:00:00:02')
            pbf.set_dst_mac('02:00:00:00:00:01')

        vmi.add_routing_instance(ri, pbf)
    # end add_pbf_rule

    def process_transparent_service(self, vm_info, v4_address, v6_address,
                                    service_ri1, service_ri2):
        vlan = self._cassandra.allocate_service_chain_vlan(vm_info['vm_uuid'],
                                                           self.name)
        self.add_pbf_rule(vm_info['left']['vmi'], service_ri1,
                          v4_address, v6_address, vlan)
        self.add_pbf_rule(vm_info['right']['vmi'], service_ri2,
                          v4_address, v6_address, vlan)
        return True
    # end process_transparent_service

    def process_in_network_service(self, vm_info, service, vn1_obj, vn2_obj,
                                   service_ri1, service_ri2, nat_service):
        service_ri1.add_service_info(
            vn2_obj, service, vm_info['left'].get('v4-address'),
            vm_info['left'].get('v6-address'),
            vn1_obj.get_primary_routing_instance().get_fq_name_str())

        if self.direction == '<>' and not nat_service:
            service_ri2.add_service_info(
                vn1_obj, service, vm_info['right'].get('v4-address'),
                vm_info['right'].get('v6-address'),
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
                RoutingInstanceST.delete(service_name1, True)
            if vn2_obj:
                service_name2 = vn2_obj.get_service_name(self.name, service)
                RoutingInstanceST.delete(service_name2, True)
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
        port_list = ["%s-%s" % (sp.start_port, sp.end_port)
                     for sp in self.sp_list]
        sc.src_ports = ','.join(port_list)
        port_list = ["%s-%s" % (dp.start_port, dp.end_port)
                     for dp in self.dp_list]
        sc.dst_ports = ','.join(port_list)
        sc.direction = self.direction
        sc.service_list = self.service_list
        sc.created = self.created
        sc.error_msg = self.error_msg
        return sc
    # end build_introspect

    def handle_st_object_req(self):
        resp = super(ServiceChain, self).handle_st_object_req()
        resp.obj_refs = [
            sandesh.RefList('service_instance', self.service_list)
        ]
        resp.properties = [
            sandesh.PropList('left_network', self.left_vn),
            sandesh.PropList('right_network', self.right_vn),
            sandesh.PropList('protocol', self.protocol),
            sandesh.PropList('src_ports',
                             ','.join(["%s-%s" % (sp.start_port, sp.end_port)
                                       for sp in self.sp_list])),
            sandesh.PropList('dst_ports',
                             ','.join(["%s-%s" % (dp.start_port, dp.end_port)
                                       for dp in self.dp_list])),
            sandesh.PropList('created', str(self.created)),
            sandesh.PropList('error_msg', str(self.error_msg)),
        ]
        return resp
    # end handle_st_object_req
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
        if not(rhs.subnet or lhs.subnet or lhs.subnet_list or rhs.subnet_list):
            return rhs.virtual_network in [lhs.virtual_network, 'any']
        l_subnets = lhs.subnet_list or []
        if lhs.subnet:
            l_subnets.append(lhs.subnet)
        l_subnets = [IPNetwork('%s/%d'%(s.ip_prefix, s.ip_prefix_len))
                     for s in l_subnets]
        r_subnets = rhs.subnet_list or []
        if rhs.subnet:
            r_subnets.append(rhs.subnet)
        r_subnets = [IPNetwork('%s/%d'%(s.ip_prefix, s.ip_prefix_len))
                     for s in r_subnets]
        for l_subnet in l_subnets:
            for r_subnet in r_subnets:
                if l_subnet in r_subnet:
                    return True
        else:
            return False
        return True


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
    prop_fields = ['bgp_router_parameters']
    ref_fields = ['bgp_as_a_service']

    def __init__(self, name, obj=None):
        self.name = name
        self.asn = None
        self.bgp_as_a_service = None
        self.vendor = None
        self.identifier = None
        self.router_type = None
        self.source_port = None
        self.update(obj)
        self.update_single_ref('bgp_as_a_service', self.obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'bgp_router_parameters' in changed:
            self.set_params(self.obj.get_bgp_router_parameters())
        return changed
    # end update

    def delete_obj(self):
        self.update_single_ref('bgp_as_a_service', {})
        if self.router_type == 'bgpaas-client':
            self._cassandra.free_bgpaas_port(self.source_port)
    # end delete_ref

    def set_params(self, params):
        self.vendor = (params.vendor or 'contrail').lower()
        self.identifier = params.identifier
        self.router_type = params.router_type
        self.source_port = params.source_port
        if self.router_type not in ('bgpaas-client', 'bgpaas-server'):
            if self.vendor == 'contrail':
                self.update_global_asn(
                    GlobalSystemConfigST.get_autonomous_system())
            else:
                self.update_autonomous_system(params.autonomous_system)
    # end set_params

    def update_global_asn(self, asn):
        if self.vendor != 'contrail' or self.asn == int(asn):
            return
        if self.router_type in ('bgpaas-client', 'bgpaas-server'):
            return
        router_obj = self.read_vnc_obj(fq_name=self.name)
        params = router_obj.get_bgp_router_parameters()
        if params.autonomous_system != int(asn):
            params.autonomous_system = int(asn)
            router_obj.set_bgp_router_parameters(params)
            self._vnc_lib.bgp_router_update(router_obj)
        self.update_autonomous_system(asn)
    # end update_global_asn

    def update_autonomous_system(self, asn):
        if self.asn == int(asn):
            return False
        self.asn = int(asn)
        self.update_peering()
        return True
    # end update_autonomous_system

    def evaluate(self, **kwargs):
        if self.router_type == 'bgpaas-client':
            bgpaas = BgpAsAServiceST.get(self.bgp_as_a_service)
            ret = self.update_bgpaas_client(bgpaas)
            if ret == -1:
                if bgpaas:
                    bgpaas.obj.del_bgp_router(self.obj)
                    try:
                        self._vnc_lib.bgp_as_a_service_update(bgpaas.obj)
                    except NoIdError:
                        pass
                vmis = self.obj.get_virtual_machine_interface_back_refs() or []
                for vmi in vmis:
                    try:
                        # remove bgp-router ref from vmi
                        self._vnc_lib.ref_update(obj_uuid=vmi['uuid'],
                                                 obj_type='virtual_machine_interface',
                                                 ref_uuid=self.obj.uuid,
                                                 ref_fq_name=self.obj.fq_name,
                                                 ref_type='bgp-router',
                                                 operation='DELETE')
                    except NoIdError:
                        pass
                try:
                    self._vnc_lib.bgp_router_delete(id=self.obj.uuid)
                    self.delete(self.name)
                except RefsExistError:
                    pass
            elif ret:
                self._vnc_lib.bgp_router_update(self.obj)
        elif self.router_type != 'bgpaas-server':
            self.update_peering()
    # end evaluate

    def update_bgpaas_client(self, bgpaas):
        if not bgpaas:
            return -1
        if bgpaas.bgpaas_shared:
            if (bgpaas.virtual_machine_interfaces and
                self.name in bgpaas.bgpaas_clients.values()):
                vmi_names = list(bgpaas.virtual_machine_interfaces)
                vmis = [VirtualMachineInterfaceST.get(vmi_name)
                        for vmi_name in vmi_names]
                vmi = vmis[0]
            elif self.name in bgpaas.bgpaas_clients.values():
                del bgpaas.bgpaas_clients[bgpaas.obj.name]
                return -1
            else:
                return -1
        else:
            for vmi_name, router in bgpaas.bgpaas_clients.items():
                if router == self.name:
                    break
            else:
                return -1
            if vmi_name not in bgpaas.virtual_machine_interfaces:
                del bgpaas.bgpaas_clients[vmi_name]
                return -1

            vmi = VirtualMachineInterfaceST.get(vmi_name)
            if vmi is None or vmi.virtual_network is None:
                del bgpaas.bgpaas_clients[vmi_name]
                return -1
            vn = VirtualNetworkST.get(vmi.virtual_network)
            if not vn or self.obj.get_parent_fq_name_str() != vn._default_ri_name:
                del bgpaas.bgpaas_clients[vmi_name]
                return -1
            vmi_names = [vmi_name]
            vmis = [vmi]

        update = False
        params = self.obj.get_bgp_router_parameters()
        if params.autonomous_system != int(bgpaas.autonomous_system):
            params.autonomous_system = int(bgpaas.autonomous_system)
            update = True
        ip = bgpaas.bgpaas_ip_address or vmi.get_primary_instance_ip_address()
        if params.address != ip:
            params.address = ip
            update = True
        if params.identifier != ip:
            params.identifier = ip
            update = True
        if bgpaas.bgpaas_suppress_route_advertisement:
            if params.gateway_address:
                params.gateway_address = None
                update = True
            if params.ipv6_gateway_address:
                params.ipv6_gateway_address = None
                update = True
        else:
            v4_gateway = vmi.get_v4_default_gateway()
            if params.gateway_address != v4_gateway:
                params.gateway_address = v4_gateway
                update = True
            if bgpaas.obj.bgpaas_ipv4_mapped_ipv6_nexthop:
                v6_gateway = vmi.get_ipv4_mapped_ipv6_gateway()
            else:
                v6_gateway = vmi.get_v6_default_gateway()
            if params.ipv6_gateway_address != v6_gateway:
                params.ipv6_gateway_address = v6_gateway
                update = True
        if update:
            self.obj.set_bgp_router_parameters(params)
        router_refs = self.obj.get_bgp_router_refs()
        peering_attribs = router_refs[0]['attr']
        if peering_attribs != bgpaas.peering_attribs:
            self.obj.set_bgp_router_list([router_refs[0]['to']],
                                         [bgpaas.peering_attribs])
            update = True

        old_refs = self.obj.get_virtual_machine_interface_back_refs() or []
        old_uuids = set([ref['uuid'] for ref in old_refs])
        new_uuids = set([vmi.obj.uuid for vmi in vmis])

        # add vmi->bgp-router link
        for vmi_id in new_uuids - old_uuids:
            self._vnc_lib.ref_update(obj_uuid=vmi_id,
                obj_type='virtual_machine_interface',
                ref_uuid=self.obj.uuid,
                ref_type='bgp_router',
                ref_fq_name=self.obj.get_fq_name_str(),
                operation='ADD')
        # remove vmi->bgp-router links for old vmi if any
        for vmi_id in old_uuids - new_uuids:
            self._vnc_lib.ref_update(obj_uuid=vmi_id,
                obj_type='virtual_machine_interface',
                ref_uuid=self.obj.uuid,
                ref_type='bgp_router',
                ref_fq_name=self.obj.get_fq_name_str(),
                operation='DELETE')
        if old_uuids != new_uuids:
            refs = [{'to': vmi.obj.fq_name, 'uuid': vmi.obj.uuid}
                       for vmi in vmis]
            self.obj.virtual_machine_interface_back_refs = refs
        return update
    # end update_bgpaas_client

    def update_peering(self):
        if not GlobalSystemConfigST.get_ibgp_auto_mesh():
            return
        if self.router_type in ('bgpaas-server', 'bgpaas-client'):
            return
        global_asn = int(GlobalSystemConfigST.get_autonomous_system())
        if self.asn != global_asn:
            return
        try:
            obj = self.read_vnc_obj(fq_name=self.name)
        except NoIdError as e:
            self._logger.error("NoIdError while reading bgp router "
                                   "%s: %s"%(self.name, str(e)))
            return

        peerings = [ref['to'] for ref in (obj.get_bgp_router_refs() or [])]
        for router in self._dict.values():
            if router.name == self.name:
                continue
            if router.router_type in ('bgpaas-server', 'bgpaas-client'):
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

        new_peerings = [ref['to'] for ref in (obj.get_bgp_router_refs() or [])]
        if new_peerings != peerings:
            try:
                self._vnc_lib.bgp_router_update(obj)
            except NoIdError as e:
                self._logger.error("NoIdError while updating bgp router "
                                   "%s: %s"%(self.name, str(e)))
    # end update_peering

    def handle_st_object_req(self):
        resp = super(BgpRouterST, self).handle_st_object_req()
        resp.properties = [
            sandesh.PropList('asn', str(self.asn)),
            sandesh.PropList('vendor', self.vendor),
            sandesh.PropList('identifier', self.identifier),
        ]
        return resp
    # end handle_st_object_req
# end class BgpRouterST


class BgpAsAServiceST(DBBaseST):
    _dict = {}
    obj_type = 'bgp_as_a_service'
    prop_fields = ['bgpaas_ip_address', 'autonomous_system',
                   'bgpaas_session_attributes',
                   'bgpaas_suppress_route_advertisement',
                   'bgpaas_ipv4_mapped_ipv6_nexthop',
                   'bgpaas_shared']
    ref_fields = ['virtual_machine_interface', 'bgp_router']

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interfaces = set()
        self.bgp_routers = set()
        self.bgpaas_clients = {}
        self.bgpaas_ip_address = None
        self.autonomous_system = None
        self.bgpaas_session_attributes = None
        self.bgpaas_suppress_route_advertisement = None
        self.bgpaas_ipv4_mapped_ipv6_nexthop = None
        self.peering_attribs = None
        self.bgpaas_shared = False
        self.update(obj)
        self.set_bgpaas_clients()
    # end __init__

    def set_bgpaas_clients(self):
        if (self.bgpaas_shared and self.bgp_routers):
            bgpr = BgpRouterST.get(list(self.bgp_routers)[0])
            self.bgpaas_clients[self.obj.name] = bgpr.obj.get_fq_name_str()
        else:
            for bgp_router in self.bgp_routers:
                bgpr = BgpRouterST.get(bgp_router)
                if bgpr is None:
                    continue
                for vmi in self.virtual_machine_interfaces:
                    if vmi.split(':')[-1] == bgpr.obj.name:
                        self.bgpaas_clients[vmi] = bgpr.obj.get_fq_name_str()
                        break
    # end set_bgp_clients

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'bgpaas_session_attributes' in changed:
            session_attrib = self.bgpaas_session_attributes
            bgp_session = BgpSession()
            if session_attrib:
                bgp_session.attributes=[session_attrib]
            peering_attribs = BgpPeeringAttributes(session=[bgp_session])
            self.peering_attribs = BgpPeeringAttributes(session=[bgp_session])
        return changed
    # end update

    def evaluate(self, **kwargs):
        # If the BGP Service is shared, just create
        # one BGP Router.
        if self.obj.get_bgpaas_shared() == True:
            if set([self.obj.name]) - set(self.bgpaas_clients.keys()):
                self.create_bgp_router(self.name, shared=True)
        else:
            for name in (self.virtual_machine_interfaces -
                         set(self.bgpaas_clients.keys())):
                self.create_bgp_router(name)
    # end evaluate

    def create_bgp_router(self, name, shared=False):
        if shared:
            if not self.obj.bgpaas_ip_address:
                return
            vmi_refs = self.obj.get_virtual_machine_interface_refs()
            if not vmi_refs:
                return
            # all vmis will have link to this bgp-router
            vmis = [VirtualMachineInterfaceST.get(':'.join(ref['to']))
                       for ref in vmi_refs]
        else:
            # only current vmi will have link to this bgp-router
            vmi = VirtualMachineInterfaceST.get(name)
            if not vmi:
                self.virtual_machine_interfaces.discard(name)
                return
            vmis = [vmi]
        vn = VirtualNetworkST.get(vmis[0].virtual_network)
        if not vn:
            return
        ri = vn.get_primary_routing_instance()
        if not ri:
            return

        server_fq_name = ri.obj.get_fq_name_str() + ':bgpaas-server'
        server_router = BgpRouterST.get(server_fq_name)
        if not server_router:
            server_router = BgpRouter('bgpaas-server', parent_obj=ri.obj)
            params = BgpRouterParams(router_type='bgpaas-server')
            server_router.set_bgp_router_parameters(params)
            self._vnc_lib.bgp_router_create(server_router)
            BgpRouterST.locate(server_fq_name, server_router)
        else:
            server_router = server_router.obj

        bgpr_name = self.obj.name if shared else vmi.obj.name
        router_fq_name = ri.obj.get_fq_name_str() + ':' + bgpr_name
        bgpr = BgpRouterST.get(router_fq_name)
        create = False
        update = False
        src_port = None
        if not bgpr:
            bgp_router = BgpRouter(bgpr_name, parent_obj=ri.obj)
            create = True
            try:
                src_port = self._cassandra.alloc_bgpaas_port(router_fq_name)
            except ResourceExhaustionError as e:
                self._logger.error("Cannot allocate BGPaaS port for %s:%s" % (
                    router_fq_name, str(e)))
                return
        else:
            bgp_router = self._vnc_lib.bgp_router_read(id=bgpr.obj.uuid)
            src_port = bgpr.source_port

        ip = self.bgpaas_ip_address or vmis[0].get_primary_instance_ip_address()
        params = BgpRouterParams(
            autonomous_system=int(self.autonomous_system) if self.autonomous_system else None,
            address=ip,
            identifier=ip,
            source_port=src_port,
            router_type='bgpaas-client')
        if bgp_router.get_bgp_router_parameters() != params:
            bgp_router.set_bgp_router(server_router, self.peering_attribs)
            update = True
        if create:
            bgp_router.set_bgp_router_parameters(params)
            bgp_router_id = self._vnc_lib.bgp_router_create(bgp_router)
            self.obj.add_bgp_router(bgp_router)
            self._vnc_lib.bgp_as_a_service_update(self.obj)
            bgpr = BgpRouterST.locate(router_fq_name)
        elif update:
            self._vnc_lib.bgp_router_update(bgp_router)
            bgp_router_id = bgp_router.uuid

        if shared:
            self.bgpaas_clients[self.obj.name] = router_fq_name
        else:
            self.bgpaas_clients[name] = router_fq_name
    # end create_bgp_router

# end class BgpAsAServiceST

class VirtualMachineInterfaceST(DBBaseST):
    _dict = {}
    obj_type = 'virtual_machine_interface'
    ref_fields = ['virtual_network', 'virtual_machine', 'port_tuple',
                  'logical_router', 'bgp_as_a_service', 'routing_instance']
    prop_fields = ['virtual_machine_interface_properties']

    def __init__(self, name, obj=None):
        self.name = name
        self.service_interface_type = None
        self.interface_mirror = None
        self.virtual_network = None
        self.virtual_machine = None
        self.port_tuples = set()
        self.logical_router = None
        self.bgp_as_a_service = None
        self.uuid = None
        self.instance_ips = set()
        self.floating_ips = set()
        self.alias_ips = set()
        self.routing_instances = {}
        self.update(obj)
        self.uuid = self.obj.uuid
        self.update_multiple_refs('instance_ip', self.obj)
        self.update_multiple_refs('floating_ip', self.obj)
        self.update_multiple_refs('alias_ip', self.obj)
        self.vrf_table = jsonpickle.encode(self.obj.get_vrf_assign_table())
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'virtual_machine_interface_properties' in changed:
            self.set_properties()
        if 'routing_instance' in changed:
            self.update_routing_instances(self.obj.get_routing_instance_refs())
        self.update_multiple_refs('port_tuple', self.obj)
        return changed
    # end update

    def delete_obj(self):
        self.update_single_ref('virtual_network', {})
        self.update_single_ref('virtual_machine', {})
        self.update_single_ref('logical_router', {})
        self.update_multiple_refs('instance_ip', {})
        self.update_multiple_refs('port_tuple', {})
        self.update_multiple_refs('floating_ip', {})
        self.update_multiple_refs('alias_ip', {})
        self.update_single_ref('bgp_as_a_service', {})
        self.update_routing_instances([])
    # end delete_obj

    def evaluate(self, **kwargs):
        self.set_virtual_network()
        self._add_pbf_rules()
        self.process_analyzer()
        self.recreate_vrf_assign_table()
    # end evaluate

    def is_left(self):
        return (self.service_interface_type == 'left')

    def is_right(self):
        return (self.service_interface_type == 'right')

    def get_any_instance_ip_address(self, ip_version=0):
        for ip_name in self.instance_ips:
            ip = InstanceIpST.get(ip_name)
            if ip is None or ip.instance_ip_address is None:
                continue
            if not ip.service_instance_ip:
                continue
            if not ip_version or ip.ip_version == ip_version:
                return ip.instance_ip_address
        return None
    # end get_any_instance_ip_address

    def get_primary_instance_ip_address(self, ip_version=4):
        for ip_name in self.instance_ips:
            ip = InstanceIpST.get(ip_name)
            if ip.is_primary() and ip.instance_ip_address and ip_version == ip.ip_version:
                return ip.instance_ip_address
        return None
    # end get_primary_instance_ip_address

    def set_properties(self):
        props = self.obj.get_virtual_machine_interface_properties()
        if props:
            service_interface_type = props.service_interface_type
            interface_mirror = props.interface_mirror
        else:
            service_interface_type = None
            interface_mirror = None
        ret = False
        if service_interface_type != self.service_interface_type:
            self.service_interface_type = service_interface_type
            ret = True
        if interface_mirror != self.interface_mirror:
            self.interface_mirror = interface_mirror
            ret = True
        return ret
    # end set_properties

    def update_routing_instances(self, ri_refs):
        routing_instances = dict((':'.join(ref['to']), ref['attr'])
                                 for ref in ri_refs or [])
        old_ri_set = set(self.routing_instances.keys())
        new_ri_set = set(routing_instances.keys())
        for ri_name in old_ri_set - new_ri_set:
            ri = RoutingInstanceST.get(ri_name)
            if ri:
                ri.virtual_machine_interfaces.discard(self.name)
        for ri_name in new_ri_set - old_ri_set:
            ri = RoutingInstanceST.get(ri_name)
            if ri:
                ri.virtual_machine_interfaces.add(self.name)
        self.routing_instances = routing_instances
    # end update_routing_instances

    def add_routing_instance(self, ri, pbf):
        if self.routing_instances.get(ri.name) == pbf:
            return
        self._vnc_lib.ref_update(
            'virtual-machine-interface', self.uuid, 'routing-instance',
            ri.obj.uuid, None, 'ADD', pbf)
        self.routing_instances[ri.name] = pbf
        ri.virtual_machine_interfaces.add(self.name)
    # end add_routing_instance

    def delete_routing_instance(self, ri):
        if ri.name not in self.routing_instances:
            return
        try:
            self._vnc_lib.ref_update(
                'virtual-machine-interface', self.uuid, 'routing-instance',
                ri.obj.uuid, None, 'DELETE')
        except NoIdError:
            # NoIdError could happen if RI is deleted while we try to remove
            # the link from VMI
            pass
        del self.routing_instances[ri.name]
        ri.virtual_machine_interfaces.discard(self.name)
    # end delete_routing_instance

    def get_virtual_machine_or_port_tuple(self):
        if self.port_tuples:
            pt_list = [PortTupleST.get(x) for x in self.port_tuples if x is not None]
            return pt_list
        elif self.virtual_machine:
            vm = VirtualMachineST.get(self.virtual_machine)
            return [vm] if vm is not None else []
        return []
    # end get_virtual_machine_or_port_tuple

    def _add_pbf_rules(self):
        if not (self.is_left() or self.is_right()):
            return

        vm_pt_list = self.get_virtual_machine_or_port_tuple()
        for vm_pt in vm_pt_list:
            if not vm_pt or vm_pt.get_service_mode() != 'transparent':
                return
            for service_chain in ServiceChain.values():
                if vm_pt.service_instance not in service_chain.service_list:
                    continue
                if not service_chain.created:
                    continue
                if self.is_left():
                    vn_obj = VirtualNetworkST.locate(service_chain.left_vn)
                    vn1_obj = vn_obj
                else:
                    vn1_obj = VirtualNetworkST.locate(service_chain.left_vn)
                    vn_obj = VirtualNetworkST.locate(service_chain.right_vn)

                service_name = vn_obj.get_service_name(service_chain.name,
                                                       vm_pt.service_instance)
                service_ri = RoutingInstanceST.get(service_name)
                v4_address, v6_address = vn1_obj.allocate_service_chain_ip(
                    service_name)
                vlan = self._cassandra.allocate_service_chain_vlan(
                    vm_pt.uuid, service_chain.name)

                service_chain.add_pbf_rule(self, service_ri, v4_address,
                                           v6_address, vlan)
        #end for vm_pt
    # end _add_pbf_rules

    def set_virtual_network(self):
        lr = LogicalRouterST.get(self.logical_router)
        if lr is not None:
            lr.update_virtual_networks()
    # end set_virtual_network

    def process_analyzer(self):
        if (self.interface_mirror is None or
            self.interface_mirror.mirror_to is None or
            self.virtual_network is None):
            return
        vn = VirtualNetworkST.get(self.virtual_network)
        if vn is None:
            return

        old_mirror_to = copy.deepcopy(self.interface_mirror.mirror_to)

        vn.process_analyzer(self.interface_mirror)

        if old_mirror_to == self.interface_mirror.mirror_to:
            return

        self.obj.set_virtual_machine_interface_properties(
            self.obj.get_virtual_machine_interface_properties())
        try:
            self._vnc_lib.virtual_machine_interface_update(self.obj)
        except NoIdError:
            self._logger.error("NoIdError while updating interface " +
                               self.name)
    # end process_analyzer

    def recreate_vrf_assign_table(self):
        if not (self.is_left() or self.is_right()):
            self._set_vrf_assign_table(None)
            return
        vn = VirtualNetworkST.get(self.virtual_network)
        if vn is None:
            self._set_vrf_assign_table(None)
            return
        vm_pt_list = self.get_virtual_machine_or_port_tuple()
        if not vm_pt_list:
            self._set_vrf_assign_table(None)
            return

        policy_rule_count = 0
        vrf_table = VrfAssignTableType()
        for vm_pt in vm_pt_list:
            smode = vm_pt.get_service_mode()
            if smode not in ['in-network', 'in-network-nat']:
                self._set_vrf_assign_table(None)
                return

            vrf_table = VrfAssignTableType()
            ip_list = []
            for ip_name in self.instance_ips:
                ip = InstanceIpST.get(ip_name)
                if ip and ip.instance_ip_address:
                    ip_list.append((ip.ip_version, ip.instance_ip_address))
            for ip_name in self.floating_ips:
                ip = FloatingIpST.get(ip_name)
                if ip and ip.floating_ip_address:
                    ip_list.append((ip.ip_version, ip.floating_ip_address))
            for ip_name in self.alias_ips:
                ip = AliasIpST.get(ip_name)
                if ip and ip.alias_ip_address:
                    ip_list.append((ip.ip_version, ip.alias_ip_address))
            for (ip_version, ip_address) in ip_list:
                if ip_version == 6:
                    address = AddressType(subnet=SubnetType(ip_address, 128))
                else:
                    address = AddressType(subnet=SubnetType(ip_address, 32))

                mc = MatchConditionType(src_address=address,
                                        protocol='any',
                                        src_port=PortType(),
                                        dst_port=PortType())

                vrf_rule = VrfAssignRuleType(match_condition=mc,
                                             routing_instance=vn._default_ri_name,
                                             ignore_acl=False)
                vrf_table.add_vrf_assign_rule(vrf_rule)

            si_name = vm_pt.service_instance
            for service_chain_list in vn.service_chains.values():
                for service_chain in service_chain_list:
                    if not service_chain.created:
                        continue
                    service_list = service_chain.service_list
                    if si_name not in service_chain.service_list:
                        continue
                    if ((si_name == service_list[0] and self.is_left()) or
                        (si_name == service_list[-1] and self.is_right())):
                        # Do not generate VRF assign rules for 'book-ends'
                        continue
                    ri_name = vn.get_service_name(service_chain.name, si_name)
                    for sp in service_chain.sp_list:
                        for dp in service_chain.dp_list:
                            if self.is_left():
                                mc = MatchConditionType(src_port=dp,
                                                        dst_port=sp,
                                                        protocol=service_chain.protocol)
                            else:
                                mc = MatchConditionType(src_port=sp,
                                                        dst_port=dp,
                                                        protocol=service_chain.protocol)

                            vrf_rule = VrfAssignRuleType(match_condition=mc,
                                                         routing_instance=ri_name,
                                                         ignore_acl=True)
                            vrf_table.add_vrf_assign_rule(vrf_rule)
                            policy_rule_count += 1
                # end for service_chain
            # end for service_chain_list
        #end for vm_pt_list
        if policy_rule_count == 0:
            vrf_table = None
        self._set_vrf_assign_table(vrf_table)
    # end recreate_vrf_assign_table

    def _set_vrf_assign_table(self, vrf_table):
        vrf_table_pickle = jsonpickle.encode(vrf_table)
        if vrf_table_pickle != self.vrf_table:
            self.obj.set_vrf_assign_table(vrf_table)
            try:
                self._vnc_lib.virtual_machine_interface_update(self.obj)
                self.vrf_table = vrf_table_pickle
            except NoIdError as e:
                if e._unknown_id == self.uuid:
                    VirtualMachineInterfaceST.delete(self.name)
    # _set_vrf_assign_table


    def handle_st_object_req(self):
        resp = super(VirtualMachineInterfaceST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('instance_ip'),
            self._get_sandesh_ref_list('floating_ip'),
            self._get_sandesh_ref_list('alias_ip'),
        ])
        resp.properties = [
            sandesh.PropList('service_interface_type',
                             self.service_interface_type),
            sandesh.PropList('interface_mirror', str(self.interface_mirror)),
        ]
        return resp
    # end handle_st_object_req

    def get_v4_default_gateway(self):
        if not self.virtual_network:
            return None
        vn = VirtualNetworkST.get(self.virtual_network)
        if not vn:
            return None
        v4_address = self.get_primary_instance_ip_address(ip_version=4)
        if not v4_address:
            return None
        return vn.get_gateway(v4_address)
    # end get_v4_default_gateway

    def get_v6_default_gateway(self):
        if not self.virtual_network:
            return None
        vn = VirtualNetworkST.get(self.virtual_network)
        if not vn:
            return None
        v6_address = self.get_primary_instance_ip_address(ip_version=6)
        if not v6_address:
            return None
        return vn.get_gateway(v6_address)
    # end get_v6_default_gateway

    def get_ipv4_mapped_ipv6_gateway(self):
        return '::ffff:%s' % self.get_v4_default_gateway()
    # end get_ipv4_mapped_ipv6_gateway
# end VirtualMachineInterfaceST


class InstanceIpST(DBBaseST):
    _dict = {}
    obj_type = 'instance_ip'
    prop_fields = ['instance_ip_address', 'service_instance_ip',
                   'instance_ip_secondary']
    ref_fields = ['virtual_machine_interface']

    def __init__(self, name, obj=None):
        self.name = name
        self.is_secondary = False
        self.virtual_machine_interfaces = set()
        self.ip_version = None
        self.instance_ip_address = None
        self.service_instance_ip = None
        self.instance_ip_secondary = False
        self.update(obj)
    # end __init

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'instance_ip_address' in changed and self.instance_ip_address:
            self.ip_version = IPAddress(self.instance_ip_address).version
        return changed
    # end update

    def is_primary(self):
        return not self.instance_ip_secondary
# end InstanceIpST


class FloatingIpST(DBBaseST):
    _dict = {}
    obj_type = 'floating_ip'
    prop_fields = ['floating_ip_address']
    ref_fields = ['virtual_machine_interface']
    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interface = None
        self.ip_version = None
        self.floating_ip_address = None
        self.update(obj)
    # end __init

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'floating_ip_address' in changed and self.floating_ip_address:
            self.ip_version = IPAddress(self.floating_ip_address).version
        return changed
    # end update
# end FloatingIpST


class AliasIpST(DBBaseST):
    _dict = {}
    obj_type = 'alias_ip'
    prop_fields = ['floating_ip_address']
    ref_fields = ['virtual_machine_interface']

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interface = None
        self.alias_ip_address = None
        self.ip_version = None
        self.update(obj)
    # end __init

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'alias_ip_address' in changed and self.alias_ip_address:
            self.ip_version = IPAddress(self.alias_ip_address).version
        return changed
    # end update
# end AliasIpST


class VirtualMachineST(DBBaseST):
    _dict = {}
    obj_type = 'virtual_machine'
    ref_fields = ['service_instance']
    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interfaces = set()
        self.service_instance = None
        self.update(obj)
        self.uuid = self.obj.uuid
        self.update_multiple_refs('virtual_machine_interface', self.obj)
    # end __init__

    def get_vmi_by_service_type(self, service_type):
        for vmi_name in self.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceST.get(vmi_name)
            if vmi and vmi.service_interface_type == service_type:
                return vmi
        return None

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

    def handle_st_object_req(self):
        resp = super(VirtualMachineST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('virtual_machine_interface'),
        ])
        resp.properties.extend([
            sandesh.PropList('service_mode', self.get_service_mode()),
        ])
        return resp
    # end handle_st_object_req
# end VirtualMachineST


class LogicalRouterST(DBBaseST):
    _dict = {}
    obj_type = 'logical_router'
    ref_fields = ['virtual_machine_interface', 'route_table']
    prop_fields = ['configured_route_target_list']
    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interfaces = set()
        self.virtual_networks = set()
        self.route_tables = set()
        self.rt_list = set()
        self.configured_route_target_list = None
        obj = obj or self.read_vnc_obj(fq_name=name)

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
                GlobalSystemConfigST.get_autonomous_system(), rtgt_num)
            rtgt_obj = RouteTargetST.locate(rt_key)
            obj.set_route_target(rtgt_obj.obj)
            self._vnc_lib.logical_router_update(obj)

        if old_rt_key:
            RouteTargetST.delete_vnc_obj(old_rt_key)
        self.route_target = rt_key
        self.update(obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if not changed:
            return False
        if 'virtual_machine_interface' in changed:
            self.update_virtual_networks()
        rt_list = self.obj.get_configured_route_target_list() or RouteTargetList()
        self.set_route_target_list(rt_list)
        return changed
    # end update

    def delete_obj(self):
        self.update_multiple_refs('virtual_machine_interface', {})
        self.update_multiple_refs('route_table', {})
        self.update_virtual_networks()
        rtgt_num = int(self.route_target.split(':')[-1])
        RouteTargetST.delete_vnc_obj(self.route_target)
        self._cassandra.free_route_target(self.name)
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
    # end update_virtual_networks

    def set_virtual_networks(self, vn_set):
        for vn in self.virtual_networks - vn_set:
            vn_obj = VirtualNetworkST.get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                ri_obj.update_route_target_list(
                    rt_add=set(), rt_del=self.rt_list|set([self.route_target]))
        for vn in vn_set - self.virtual_networks:
            vn_obj = VirtualNetworkST.get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                ri_obj.update_route_target_list(
                    rt_add=self.rt_list|set([self.route_target]))
        self.virtual_networks = vn_set
    # end set_virtual_networks

    def update_autonomous_system(self, asn):
        old_rt = self.route_target
        old_asn = int(old_rt.split(':')[1])
        if int(asn) == old_asn:
            return
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
        RouteTargetST.delete_vnc_obj(old_rt)
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
                ri_obj.update_route_target_list(rt_del=rt_del, rt_add=rt_add)
    # end set_route_target_list

    def handle_st_object_req(self):
        resp = super(LogicalRouterST, self).handle_st_object_req()
        resp.obj_refs.extend([
            sandesh.RefList('route_target', self.rt_list),
        ])
        resp.properties.extend([
            sandesh.PropList('route_target', self.route_target),
        ])
        return resp
    # end handle_st_object_req
# end LogicalRouterST


class ServiceInstanceST(DBBaseST):
    _dict = {}
    obj_type = 'service_instance'
    ref_fields = ['virtual_machine', 'service_template']
    prop_fields = ['service_instance_properties']
    vn_dict = {}

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machines = set()
        self.service_template = None
        self.auto_policy = False
        self.left_vn_str = None
        self.right_vn_str = None
        self.routing_policys = {}
        self.port_tuples = set()
        self.route_aggregates = {}
        self.update(obj)
        self.network_policys = NetworkPolicyST.get_by_service_instance(self.name)
        self.route_tables = RouteTableST.get_by_service_instance(self.name)
        for ref in self.obj.get_routing_policy_back_refs() or []:
            self.routing_policys[':'.join(ref['to'])] = ref['attr']
        for ref in self.obj.get_route_aggregate_back_refs() or []:
            self.route_aggregates[':'.join(ref['to'])] = ref['attr']['interface_type']
        self.set_children('port_tuple', self.obj)
    # end __init__

    def update(self, obj=None):
        self.unset_vn_si_mapping()
        changed = self.update_vnc_obj(obj)
        if 'service_template' in changed:
            st_refs = self.obj.get_service_template_refs()
            if st_refs:
                self.service_template = ':'.join(st_refs[0]['to'])
        if 'service_instance_properties' in changed:
            changed.remove('service_instance_properties')
            props = self.obj.get_service_instance_properties()
            if props:
                changed = self.add_properties(props)
        self.set_vn_si_mapping()
        return changed
    # end update

    def get_allocated_interface_ip(self, side, version):
        vm_pt_list = []
        for vm_name in  self.virtual_machines or []:
            vm_pt_list.append(VirtualMachineST.get(vm_name))
        for pt_name in  self.port_tuples or []:
            vm_pt_list.append(PortTupleST.get(pt_name))
        for vm_pt in vm_pt_list:
            if not vm_pt:
                continue
            vmi = vm_pt.get_vmi_by_service_type(side)
            if not vmi:
                continue
            return vmi.get_any_instance_ip_address(version)
        return None

    def get_virtual_networks(self, si_props):
        left_vn = None
        right_vn = None

        st_refs = self.obj.get_service_template_refs()
        uuid = st_refs[0]['uuid']
        st_obj = DBBaseST().read_vnc_obj(uuid, obj_type='service_template')
        st_props = st_obj.get_service_template_properties()

        st_if_list = st_props.get_interface_type() or []
        si_if_list = si_props.get_interface_list() or []
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
        left_vn_str, right_vn_str = self.get_virtual_networks(props)
        ret = (self.auto_policy == props.auto_policy)
        if (left_vn_str, right_vn_str) != (self.left_vn_str, self.right_vn_str):
            self.left_vn_str = left_vn_str
            self.right_vn_str = right_vn_str
            ret = True
        if not props.auto_policy:
            self.delete_properties()
            return ret
        self.auto_policy = True
        if (not self.left_vn_str or not self.right_vn_str):
            self._logger.error(
                "%s: route table next hop service instance must "
                "have left and right virtual networks" % self.name)
            self.delete_properties()
            return ret

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
        vn1 = VirtualNetworkST.get(self.left_vn_str)
        if vn1:
            vn1.add_policy(policy_name)
        vn2 = VirtualNetworkST.get(self.right_vn_str)
        if vn2:
            vn2.add_policy(policy_name)
    # add_properties

    def set_vn_si_mapping(self):
        if self.left_vn_str:
            ServiceInstanceST.vn_dict.setdefault(self.left_vn_str,
                                                 set()).add(self)
    # end set_vn_si_mapping

    def unset_vn_si_mapping(self):
        if self.left_vn_str:
            try:
                ServiceInstanceST.vn_dict[self.left_vn_str].remove(self)
                if not ServiceInstanceST.vn_dict[self.left_vn_str]:
                    del ServiceInstanceST.vn_dict[self.left_vn_str]
            except KeyError:
                pass
    # end unset_vn_si_mapping

    @classmethod
    def get_vn_si_mapping(cls, vn_str):
        if vn_str:
            return ServiceInstanceST.vn_dict.get(vn_str)
        return None
    # end get_vn_si_mapping

    def delete_properties(self):
        policy_name = '_internal_' + self.name
        policy = NetworkPolicyST.get(policy_name)
        if policy is None:
            return
        for vn_name in policy.virtual_networks:
            vn = VirtualNetworkST.get(vn_name)
            if vn is None:
                continue
            del vn.network_policys[policy_name]
        # end for vn_name
        NetworkPolicyST.delete(policy_name)
    # end delete_properties

    def delete_obj(self):
        self.unset_vn_si_mapping()
        self.update_multiple_refs('virtual_machine', {})
        self.delete_properties()
    # end delete_obj

    def _update_service_template(self):
        if self.service_template is None:
            self._logger.error("service template is None for service instance "
                               + self.name)
            return
        try:
            st_obj = self.read_vnc_obj(fq_name=self.service_template,
                                       obj_type='service_template')
        except NoIdError:
            self._logger.error("NoIdError while reading service template " +
                               self.service_template)
            return
        st_props = st_obj.get_service_template_properties()
        self.service_mode = st_props.get_service_mode() or 'transparent'
        self.virtualization_type = st_props.get_service_virtualization_type()
    # end get_service_mode

    def get_service_mode(self):
        if hasattr(self, 'service_mode'):
            return self.service_mode
        self._update_service_template()
        return getattr(self, 'service_mode', None)
    # end get_service_mode

    def get_virtualization_type(self):
        if hasattr(self, 'virtualization_type'):
            return self.virtualization_type
        self._update_service_template()
        return getattr(self, 'virtualization_type', None)
    # end get_virtualization_type

    def handle_st_object_req(self):
        resp = super(ServiceInstanceST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('port_tuple'),
            self._get_sandesh_ref_list('network_policy'),
            self._get_sandesh_ref_list('route_table'),
        ])
        resp.properties.extend([
            sandesh.PropList('left_network', self.left_vn_str),
            sandesh.PropList('right_network', self.right_vn_str),
            sandesh.PropList('auto_policy', str(self.auto_policy)),
            sandesh.PropList('service_mode', self.get_service_mode()),
            sandesh.PropList('virtualization_type',
                             self.get_virtualization_type()),
        ])
        return resp
    # end handle_st_object_req
# end ServiceInstanceST


class RoutingPolicyST(DBBaseST):
    _dict = {}
    obj_type = 'routing_policy'
    ref_fields = ['service_instance']

    def __init__(self, name, obj=None):
        self.name = name
        self.service_instances = {}
        self.routing_instances = set()
        self.update(obj)
        ri_refs = self.obj.get_routing_instance_refs() or []
        for ref in ri_refs:
            ri_name = ':'.join(ref['to'])
            self.routing_instances.add(ri_name)
            ri = RoutingInstanceST.get(ri_name)
            if ri:
                ri.routing_policys[self.name] = ref['attr'].sequence
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'service_instance' not in changed:
            return False
        new_refs = dict((':'.join(ref['to']), ref['attr'])
                        for ref in self.obj.get_service_instance_refs() or [])
        for ref in set(self.service_instances.keys()) - set(new_refs.keys()):
            si = ServiceInstanceST.get(ref)
            if si and self.name in si.routing_policys:
                del si.routing_policys[self.name]
        for ref in set(new_refs.keys()):
            si = ServiceInstanceST.get(ref)
            if si:
                si.routing_policys[self.name] = new_refs[ref]
        self.service_instances = new_refs
        return True
    # end update

    def add_routing_instance(self, ri, seq):
        if ri.name in self.routing_instances:
            return
        self._vnc_lib.ref_update('routing-policy', self.obj.uuid,
                                 'routing-instance', ri.obj.uuid,
                                 None, 'ADD', RoutingPolicyType(seq))
        self.routing_instances.add(ri.name)
    # end add_routing_instance

    def delete_routing_instance(self, ri):
        if ri.name not in self.routing_instances:
            return
        self._vnc_lib.ref_update('routing-policy', self.obj.uuid,
                                 'routing-instance', ri.obj.uuid,
                                 None, 'DELETE')
        self.routing_instances.discard(ri.name)
    # end delete_routing_instance

    def handle_st_object_req(self):
        resp = super(RoutingPolicyST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('routing_instance'),
        ])
        return resp
    # end handle_st_object_req
# end RoutingPolicyST

class RouteAggregateST(DBBaseST):
    _dict = {}
    obj_type = 'route_aggregate'
    prop_fields = ['aggregate_route_entries']
    ref_fields = ['service_instance', 'routing_instance']

    def __init__(self, name, obj=None):
        self.name = name
        self.service_instances = {}
        self.routing_instances = set()
        self.aggregate_route_entries = None
        self.update(obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'service_instance' in changed:
            new_refs = dict((':'.join(ref['to']), ref['attr'].interface_type)
                            for ref in self.obj.get_service_instance_refs() or [])
            for ref in set(self.service_instances.keys()) - set(new_refs.keys()):
                si = ServiceInstanceST.get(ref)
                if si and self.name in si.route_aggregates:
                    del si.route_aggregates[self.name]
            for ref in set(new_refs.keys()):
                si = ServiceInstanceST.get(ref)
                if si:
                    si.route_aggregates[self.name] = new_refs[ref]
            self.service_instances = new_refs
        return changed
    # end update

    def add_routing_instance(self, ri):
        if ri.name in self.routing_instances:
            return
        if not self.aggregate_route_entries or not self.aggregate_route_entries.route:
            return
        ip_version = IPNetwork(self.aggregate_route_entries.route[0]).version
        if ip_version == 4:
            if ri.service_chain_info is None:
                self._logger.error("No ipv4 service chain info found for %s"
                                   % ri.name)
                return
            next_hop = ri.service_chain_info.get_service_chain_address()
        elif ip_version == 6:
            if ri.v6_service_chain_info is None:
                self._logger.error("No ipv6 service chain info found for %s"
                                   % ri.name)
                return
            next_hop = ri.v6_service_chain_info.get_service_chain_address()
        else:
            self._logger.error("route aggregate %s: unknonwn ip version: %s"
                               % (self.name, ip_version))
            return
        self.obj.set_aggregate_route_nexthop(next_hop)
        self.obj.set_routing_instance(ri.obj)
        self._vnc_lib.route_aggregate_update(self.obj)
        self.routing_instances.add(ri.name)
    # end add_routing_instance

    def delete_routing_instance(self, ri):
        if ri.name not in self.routing_instances:
            return
        self.obj.set_aggregate_route_nexthop(None)
        self.obj.set_routing_instance_list([])
        try:
            self._vnc_lib.route_aggregate_update(self.obj)
        except NoIdError:
            pass
        self.routing_instances.discard(ri.name)
    # end delete_routing_instance

    def handle_st_object_req(self):
        resp = super(RouteAggregateST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('routing_instance'),
        ])
        return resp
    # end handle_st_object_req
# end RouteAggregateST


class PortTupleST(DBBaseST):
    _dict = {}
    obj_type = 'port_tuple'

    def __init__(self, name, obj=None):
        self.name = name
        self.service_instance = None
        self.virtual_machine_interfaces = set()
        self.update(obj)
        self.uuid = self.obj.uuid
        self.add_to_parent(self.obj)
        self.update_multiple_refs('virtual_machine_interface', self.obj)
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(fq_name=self.name)
        return False
    # end update

    def delete_obj(self):
        self.update_multiple_refs('virtual_machine_interface', {})
        self.remove_from_parent()
    # end delete_obj

    def get_vmi_by_service_type(self, service_type):
        for vmi_name in self.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceST.get(vmi_name)
            if vmi and vmi.service_interface_type == service_type:
                return vmi
        return None

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

    def handle_st_object_req(self):
        resp = super(PortTupleST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('virtual_machine_interface'),
            self._get_sandesh_ref_list('service_instance'),
        ]
        return resp
    # end handle_st_object_req
# end PortTupleST
