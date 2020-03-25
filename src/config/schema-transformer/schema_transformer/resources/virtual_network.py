#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from builtins import object
try:
    # python2.7
    from collections import OrderedDict
except Exception:
    # python2.6
    from ordereddict import OrderedDict
from builtins import str
import copy
import itertools
import sys

import cfgm_common as common
from cfgm_common.exceptions import NoIdError, RefsExistError
from cfgm_common.uve.virtual_network.ttypes import UveVirtualNetworkConfig
from cfgm_common.uve.virtual_network.ttypes import UveVirtualNetworkConfigTrace
from netaddr import IPNetwork
from vnc_api.gen.resource_client import InstanceIp
from vnc_api.gen.resource_xsd import AclEntriesType, AclRuleType
from vnc_api.gen.resource_xsd import ActionListType, AddressType
from vnc_api.gen.resource_xsd import InstanceTargetType, MatchConditionType
from vnc_api.gen.resource_xsd import PortType, SequenceType
from vnc_api.gen.resource_xsd import VirtualNetworkPolicyType
from vnc_api.gen.resource_xsd import VirtualNetworkType

from schema_transformer.resources._access_control_list import \
    _access_control_list_update
from schema_transformer.resources._resource_base import ResourceBaseST
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
from schema_transformer.utils import _PROTO_STR_TO_NUM_IPV4
from schema_transformer.utils import _PROTO_STR_TO_NUM_IPV6
from schema_transformer.utils import RULE_IMPLICIT_ALLOW_UUID
from schema_transformer.utils import RULE_IMPLICIT_DENY_UUID


# a struct to store attributes related to Virtual Networks needed by
# schema transformer
class VirtualNetworkST(ResourceBaseST):
    _dict = {}
    obj_type = 'virtual_network'
    ref_fields = ['network_policy', 'virtual_machine_interface',
                  'route_table', 'bgpvpn', 'network_ipam',
                  'virtual_network', 'routing_policy']
    prop_fields = ['virtual_network_properties', 'route_target_list',
                   'multi_policy_service_chains_enabled',
                   'is_provider_network', 'fabric_snat']

    def me(self, name):
        return name in (self.name, 'any')

    def __init__(self, name, obj=None, acl_dict=None):
        self.name = name
        self.network_policys = OrderedDict()
        self.routing_policys = {}
        self.virtual_machine_interfaces = set()
        self.connections = set()
        self.routing_instances = set()
        self.virtual_networks = set()
        self.bgpvpns = set()
        self.acl = None
        self.is_provider_network = False
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
        self.fabric_snat = False
        self.ipams = {}
        self._route_target = None
        self.rt_list = set()
        self.import_rt_list = set()
        self.export_rt_list = set()
        self.bgpvpn_rt_list = set()
        self.bgpvpn_import_rt_list = set()
        self.bgpvpn_export_rt_list = set()
        self.route_tables = set()
        self.service_chains = {}
        prop = self.obj.get_virtual_network_properties(
        ) or VirtualNetworkType()
        self.allow_transit = prop.allow_transit
        if self.obj.get_fq_name() == common.LINK_LOCAL_VN_FQ_NAME:
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
            rt_obj = ResourceBaseST.get_obj_type_map().get(
                'route_table').get(rt)
            if rt_obj:
                routes |= set(rt_obj.routes)
        return routes
    # end

    def update(self, obj=None):
        ret = self.update_vnc_obj(obj)

        old_policies = set(self.network_policys.keys())
        self.network_policys = OrderedDict()
        for policy_ref in self.obj.get_network_policy_refs() or []:
            self.add_policy(':'.join(policy_ref['to']),
                            policy_ref.get('attr'))

        for policy_name in ResourceBaseST.get_obj_type_map().get(
                'network_policy').get_internal_policies(self.name):
            self.add_policy(policy_name)
        for policy_name in old_policies - set(self.network_policys.keys()):
            policy = ResourceBaseST.get_obj_type_map().get(
                'network_policy').get(policy_name)
            if policy:
                policy.virtual_networks.discard(self.name)

        for policy_name in set(self.network_policys.keys()) - old_policies:
            policy = ResourceBaseST.get_obj_type_map().get(
                'network_policy').get(policy_name)
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
        service_ri = ResourceBaseST.get_obj_type_map().get(
            'routing_instance').get(service_ri_name)
        if service_ri is None:
            return
        if (multi_policy_enabled and
                service_ri_name in primary_ri.connections):
            primary_ri.delete_connection(service_ri)
            # add primary ri's route target to service ri
            rt_obj = ResourceBaseST.get_obj_type_map().get(
                'route_target').get(self.get_route_target())
            service_ri.obj.add_route_target(rt_obj.obj,
                                            InstanceTargetType('import'))
            self._vnc_lib.routing_instance_update(service_ri.obj)
        elif (not multi_policy_enabled and
                service_ri_name not in primary_ri.connections):
            primary_ri.add_connection(service_ri)
            # delete primary ri's route target from service ri
            rt_obj = ResourceBaseST.get_obj_type_map().get(
                'route_target').get(self.get_route_target())
            service_ri.obj.del_route_target(rt_obj.obj)
            self._vnc_lib.routing_instance_update(service_ri.obj)
    # end _update_primary_ri_to_service_ri_connection

    def check_multi_policy_service_chain_status(self):
        if not self.multi_policy_service_chains_status_changed:
            return
        self.multi_policy_service_chains_status_changed = False
        for sc_list in list(self.service_chains.values()):
            for sc in sc_list:
                if sc is None or not sc.created:
                    continue
                if sc.left_vn == self.name:
                    si_name = sc.service_list[0]
                    other_vn_name = sc.right_vn
                    other_si_name = sc.service_list[-1]
                    right_si = ResourceBaseST.get_obj_type_map().get(
                        'service_instance').get(other_si_name)
                elif sc.right_vn == self.name:
                    si_name = sc.service_list[-1]
                    other_vn_name = sc.left_vn
                    other_si_name = sc.service_list[0]
                    right_si = ResourceBaseST.get_obj_type_map().get(
                        'service_instance').get(si_name)
                else:
                    continue
                other_vn = VirtualNetworkST.get(other_vn_name)
                if not other_vn:
                    continue
                multi_policy_enabled = (
                    self.multi_policy_service_chains_enabled and
                    other_vn.multi_policy_service_chains_enabled and
                    right_si.get_service_mode() != 'in-network-nat')
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
            policy = ResourceBaseST.get_obj_type_map().get(
                'network_policy').get(policy_name)
            if not policy:
                continue
            policy.virtual_networks.discard(self.name)
        self.update_multiple_refs('virtual_machine_interface', {})
        self.delete_inactive_service_chains(self.service_chains)
        for ri_name in self.routing_instances:
            ri = ResourceBaseST.get_obj_type_map().get(
                'routing_instance').get(ri_name)
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

        self.update_multiple_refs('route_table', {})
        self.update_multiple_refs('bgpvpn', {})
        self.uve_send(deleted=True)
    # end delete_obj

    def add_policy(self, policy_name, attrib=None):
        # Add a policy ref to the vn. Keep it sorted by sequence number
        if attrib is None:
            attrib = VirtualNetworkPolicyType(SequenceType(sys.maxsize,
                                                           sys.maxsize))
        if attrib.sequence is None:
            self._logger.error("Cannot assign policy %s to %s: sequence "
                               "number is not available" % (policy_name,
                                                            self.name))
            return False

        if self.network_policys.get(policy_name) == attrib:
            return False
        self.network_policys[policy_name] = attrib
        self.network_policys = OrderedDict(
            sorted(list(self.network_policys.items()),
                   key=lambda t: (t[1].sequence.major,
                                  t[1].sequence.minor)))
        return True
    # end add_policy

    def get_primary_routing_instance(self):
        return ResourceBaseST.get_obj_type_map().get(
            'routing_instance').get(self._default_ri_name)
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
                service_chain = ResourceBaseST.get_obj_type_map().get(
                    'service_chain').find_or_create(
                    remote_vn, self.name, prule.direction, prule.src_ports,
                    prule.dst_ports, proto, services)
                service_chain_ris[remote_vn] = (
                    None, self.get_service_name(service_chain.name,
                                                services[-1]))
            else:
                service_chain = ResourceBaseST.get_obj_type_map().get(
                    'service_chain').find_or_create(
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
        return set((k) for k, v in list(self._dict.items())
                   if (self.name != v.name and
                       self.obj.get_parent_fq_name() ==
                       v.obj.get_parent_fq_name()))
    # end get_vns_in_project

    def create_instance_ip(self, sc_name, family, ipam_obj):
        iip_name = sc_name + '-' + family
        iip_obj = InstanceIp(name=iip_name, instance_ip_family=family)
        iip_obj.set_instance_ip_secondary(True)
        iip_obj.add_network_ipam(ipam_obj)
        try:
            self._vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            pass
        except Exception as e:
            self._logger.error(
                "Error while allocating ip%s address in network-ipam %s: %s"
                % (family, ipam_obj.name, str(e)))
            return None, None
        iip_obj = self._vnc_lib.instance_ip_read(fq_name=[iip_name])
        iip_uuid = iip_obj.get_uuid()
        iip_address = iip_obj.get_instance_ip_address()
        return iip_uuid, iip_address
    # end create_instance_ip #yijie

    def allocate_service_chain_ip(self, sc_fq_name):
        sc_name = sc_fq_name.split(':')[-1]
        ip_dict = self._object_db.get_service_chain_ip(sc_name)
        if ip_dict:
            return ip_dict.get('ip_address'), ip_dict.get('ipv6_address')
        ip_dict = {}
        sc_ipam_obj = ResourceBaseST.get_obj_type_map().get(
            'service_chain')._get_service_chain_ipam()
        v4_iip_uuid, v4_address = \
            self.create_instance_ip(sc_name, 'v4', ipam_obj=sc_ipam_obj)
        if v4_iip_uuid:
            ip_dict['ip_address'] = v4_address
            ip_dict['ip_uuid'] = v4_iip_uuid
        v6_iip_uuid, v6_address = \
            self.create_instance_ip(sc_name, 'v6', ipam_obj=sc_ipam_obj)
        if v6_iip_uuid:
            ip_dict['ipv6_address'] = v6_address
            ip_dict['ipv6_uuid'] = v6_iip_uuid
        if v4_iip_uuid is None and v6_iip_uuid is None:
            return None, None
        self._object_db.add_service_chain_ip(sc_name, ip_dict)
        return v4_address, v6_address
    # end allocate_service_chain_ip

    def free_service_chain_ip(self, sc_fq_name):
        sc_name = sc_fq_name.split(':')[-1]
        ip_dict = self._object_db.get_service_chain_ip(sc_name)
        if not ip_dict:
            return
        if ip_dict.get('ip_uuid'):
            try:
                self._vnc_lib.instance_ip_delete(id=ip_dict['ip_uuid'])
            except NoIdError:
                pass
        elif ip_dict.get('ip_address'):
            # To Handle old scheme
            try:
                self._vnc_lib.virtual_network_ip_free(self.obj,
                                                      ip_dict['ip_address'])
            except NoIdError:
                pass

        if ip_dict.get('ipv6_uuid'):
            try:
                self._vnc_lib.instance_ip_delete(id=ip_dict['ipv6_uuid'])
            except NoIdError:
                pass
        elif ip_dict.get('ipv6_address'):
            # To Handle old scheme
            try:
                self._vnc_lib.virtual_network_ip_free(self.obj,
                                                      ip_dict['ipv6_address'])
            except NoIdError:
                pass
        self._object_db.remove_service_chain_ip(sc_name)
    # end free_service_chain_ip

    def get_route_target(self):
        if self._route_target is None:
            ri = self.get_primary_routing_instance()
            if ri is None:
                return
            ri_name = ri.get_fq_name_str()
            self._route_target = self._object_db.get_route_target(ri_name)
        rtgt_name = "target:%s:%d" % (
                    ResourceBaseST.get_obj_type_map().get(
                        'global_system_config').get_autonomous_system(),
                    self._route_target)
        return rtgt_name
    # end get_route_target

    def set_route_target(self, rtgt_name):
        _, asn, target = rtgt_name.split(':')
        if int(asn) != ResourceBaseST.get_obj_type_map().get(
                'global_system_config').get_autonomous_system():
            return
        self._route_target = int(target)
    # end set_route_target

    @staticmethod
    def _ri_needs_external_rt(vn_name, ri_name):
        sc_id = ResourceBaseST.get_obj_type_map().get(
            'routing_instance')._get_service_id_from_ri(ri_name)
        if sc_id is None:
            return False
        sc = ResourceBaseST.get_obj_type_map().get(
            'service_chain').get(sc_id)
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
        for sc_list in list(self.service_chains.values()):
            for service_chain in sc_list:
                service_chain.update_ipams(self.name)
    # end update_ipams

    def expand_connections(self):
        if '*' in self.connections:
            conn = self.connections - set(['*']) | self.get_vns_in_project()
            for vn in list(self._dict.values()):
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
        for sc_list in list(self.service_chains.values()):
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
                ri = ResourceBaseST.get_obj_type_map().get(
                    'routing_instance').get(ri_name)
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
                    ri.update_route_target_list(
                        rt_del=[self.get_route_target()])
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

        # Get BGP VPN's route targets associated to that network
        self.bgpvpn_rt_list = set()
        self.bgpvpn_import_rt_list = set()
        self.bgpvpn_export_rt_list = set()
        for bgpvpn_name in self.bgpvpns:
            bgpvpn = ResourceBaseST.get_obj_type_map().get(
                'bgpvpn').get(bgpvpn_name)
            if bgpvpn is not None:
                self.bgpvpn_rt_list |= bgpvpn.rt_list
                self.bgpvpn_import_rt_list |= bgpvpn.import_rt_list
                self.bgpvpn_export_rt_list |= bgpvpn.export_rt_list

        # if any RT exists in both import and export, just add it to rt_list
        self.rt_list |= self.import_rt_list & self.export_rt_list
        # if any RT exists in rt_list, remove it from import/export lists
        self.import_rt_list -= self.rt_list
        self.export_rt_list -= self.rt_list
    # end get_route_target_lists

    def set_route_target_list(self, obj):
        old_rt_list = self.rt_list.copy()
        old_import_rt_list = self.import_rt_list.copy()
        old_export_rt_list = self.export_rt_list.copy()
        old_bgpvpn_rt_list = self.bgpvpn_rt_list.copy()
        old_bgpvpn_import_rt_list = self.bgpvpn_import_rt_list.copy()
        old_bgpvpn_export_rt_list = self.bgpvpn_export_rt_list.copy()

        self.get_route_target_lists(obj)
        rt_add = ((self.rt_list - old_rt_list) |
                  (self.bgpvpn_rt_list - old_bgpvpn_rt_list))
        rt_add_import = ((self.import_rt_list - old_import_rt_list) |
                         (self.bgpvpn_import_rt_list -
                          old_bgpvpn_import_rt_list))
        rt_add_export = ((self.export_rt_list - old_export_rt_list) |
                         (self.bgpvpn_export_rt_list -
                          old_bgpvpn_export_rt_list))
        rt_del = (
            ((old_rt_list - self.rt_list) |
             (old_import_rt_list - self.import_rt_list) |
             (old_export_rt_list - self.export_rt_list) |
             (old_bgpvpn_rt_list - self.bgpvpn_rt_list) |
             (old_bgpvpn_import_rt_list - self.bgpvpn_import_rt_list) |
             (old_bgpvpn_export_rt_list - self.bgpvpn_export_rt_list)) -
            (self.rt_list | self.import_rt_list | self.export_rt_list |
             self.bgpvpn_rt_list | self.bgpvpn_import_rt_list |
             self.bgpvpn_export_rt_list)
        )
        if not (rt_add or rt_add_export or rt_add_import or rt_del):
            return False
        for rt in itertools.chain(rt_add, rt_add_export, rt_add_import):
            ResourceBaseST.get_obj_type_map().get('route_target').locate(rt)

        ri = self.get_primary_routing_instance()
        if ri:
            ri.update_route_target_list(rt_add=rt_add,
                                        rt_add_import=rt_add_import,
                                        rt_add_export=rt_add_export,
                                        rt_del=rt_del)
        for ri_name in self.routing_instances:
            if self._ri_needs_external_rt(self.name, ri_name):
                service_ri = ResourceBaseST.get_obj_type_map().get(
                    'routing_instance').get(ri_name)
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
                ResourceBaseST.get_obj_type_map().get(
                    'route_target').delete_vnc_obj(rt)
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
        si = ResourceBaseST.get_obj_type_map().get(
            'service_instance').get(next_hop)
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
        si = ResourceBaseST.get_obj_type_map().get(
            'service_instance').get(analyzer_name)
        if si is None:
            return (None, None)
        vm_analyzer = list(si.virtual_machines)
        pt_list = list(si.port_tuples)
        if not vm_analyzer and not pt_list:
            return (None, None)
        if pt_list:
            vm_pt = ResourceBaseST.get_obj_type_map().get(
                'port_tuple').get(pt_list[0])
        else:
            vm_pt = ResourceBaseST.get_obj_type_map().get(
                'virtual_machine').get(vm_analyzer[0])
        if vm_pt is None:
            return (None, None)
        vmis = vm_pt.virtual_machine_interfaces
        for vmi_name in vmis:
            vmi = ResourceBaseST.get_obj_type_map().get(
                'virtual_machine_interface').get(vmi_name)
            if vmi and vmi.is_left():
                vn_analyzer = vmi.virtual_network
                ip_version = None
                if vmi.get_primary_instance_ip_address(ip_version=4):
                    ip_version = 4
                elif vmi.get_primary_instance_ip_address(ip_version=6):
                    ip_version = 6
                ip_analyzer = vmi.get_any_instance_ip_address(ip_version)
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
        else:  # IPv4
            return _PROTO_STR_TO_NUM_IPV4.get(pproto.lower())
    # end protocol_policy_to_acl

    def address_list_policy_to_acl(self, addr):
        if addr.network_policy:
            pol = ResourceBaseST.get_obj_type_map().get(
                'network_policy').get(addr.network_policy)
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

        arule_proto = self.protocol_policy_to_acl(prule.protocol,
                                                  prule.ethertype)
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
                if self.me(dvn) or self.me(svn):
                    pass
                elif not dvn and dpol:
                    dp_obj = ResourceBaseST.get_obj_type_map().get(
                        'network_policy').get(dpol)
                    if self.name in dp_obj.virtual_networks:
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
                    sp_obj = ResourceBaseST.get_obj_type_map().get(
                        'network_policy').get(spol)
                    if self.name in sp_obj.virtual_networks:
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
                        service_ri = service_ris.get(da.virtual_network,
                                                     [None])[0]
                    elif self.me(da.virtual_network):
                        service_ri = service_ris.get(sa.virtual_network,
                                                     [None, None])[1]
                    acl = self.add_acl_rule(
                        sa, sp, da, dp, arule_proto, rule_uuid,
                        prule.action_list, prule.direction, service_ri)
                    result_acl_rule_list.append(acl)
                # end for sp, dp
            # end for daddr
        # end for saddr
        return result_acl_rule_list
    # end policy_to_acl_rule

    def add_acl_rule(self, sa, sp, da, dp, proto, rule_uuid, action, direction,
                     service_ri=None):
        action_list = copy.deepcopy(action)
        action_list.set_assign_routing_instance(service_ri)
        match = MatchConditionType(proto, sa, sp, da, dp)
        acl = AclRuleType(match, action_list, rule_uuid, direction)
        return acl

    def update_pnf_presence(self):
        has_pnf = False
        for ri_name in self.routing_instances:
            if ri_name == self._default_ri_name:
                continue
            ri = ResourceBaseST.get_obj_type_map().get(
                'routing_instance').get(ri_name)
            if ri is None:
                continue
            if ri.obj.get_routing_instance_has_pnf():
                has_pnf = True
                break
        default_ri = ResourceBaseST.get_obj_type_map().get(
            'routing_instance').get(self._default_ri_name)
        if (default_ri and
                default_ri.obj.get_routing_instance_has_pnf() != has_pnf):
            default_ri.obj.set_routing_instance_has_pnf(has_pnf)
            self._vnc_lib.routing_instance_update(default_ri.obj)
    # end update_pnf_presence

    def evaluate(self, **kwargs):
        self.timer = kwargs.get('timer')
        self.set_route_target_list(self.obj)

        old_virtual_network_connections = self.expand_connections()
        old_service_chains = self.service_chains
        self.connections = set()
        self.service_chains = {}
        self.acl_rule_count = 0

        static_acl_entries = None
        dynamic_acl_entries = None
        # add a static acl in case of provider-network
        if (not self.network_policys and
            (self.is_provider_network or self.virtual_networks or
             self.fabric_snat)):
            static_acl_entries = AclEntriesType(dynamic=False)

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
            policy = ResourceBaseST.get_obj_type_map().get(
                'network_policy').get(policy_name)
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
                    connected_networks = set()
                    if self.me(match.dst_address.virtual_network):
                        connected_networks.add(
                            match.src_address.virtual_network)
                    if self.me(match.src_address.virtual_network):
                        connected_networks.add(
                            match.dst_address.virtual_network)
                    for connected_network in connected_networks:
                        if action.apply_service:
                            # if a service was applied, the ACL should have a
                            # pass action, and we should not make a connection
                            # between the routing instances
                            action.simple_action = "pass"
                            action.apply_service = []
                            if self.multi_policy_service_chains_enabled:
                                other_vn = VirtualNetworkST.get(
                                    connected_network)
                                if not other_vn:
                                    continue
                                # check to see if service chain(s) between the
                                # two networks associated with a policy has
                                # service of type 'in-network-nat, in which
                                # case we shouldn't connect the two networks
                                # directly
                                nat_service = False
                                sc_list = \
                                    self.service_chains[connected_network]
                                for sc in sc_list:
                                    if sc is not None and sc.created:
                                        right_si_name = sc.service_list[-1]
                                        right_si = \
                                            ResourceBaseST.get_obj_type_map(
                                            ).get('service_instance').get(
                                                right_si_name)
                                        if (right_si.get_service_mode() ==
                                                'in-network-nat'):
                                            nat_service = True
                                            break

                                if (other_vn.
                                        multi_policy_service_chains_enabled and
                                        not nat_service):
                                    self.add_connection(connected_network)
                            continue

                        if action.simple_action:
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
            acl = AclRuleType(match, action, RULE_IMPLICIT_ALLOW_UUID)
            acl_list.append(acl)

            # Block fbaric snat enabled VNs to access other VNs.
            # Allow this-vn to fab-vn and block this-vn to any-other-vn traffic
            if self.fabric_snat:
                # Allow this-vn to fab-vn trffic
                this_vn_to_provider_acl = self.add_acl_rule(
                    AddressType(virtual_network=self.name),
                    PortType(),
                    AddressType(virtual_network='default-domain:'
                                                'default-project:ip-fabric'),
                    PortType(),
                    "any",
                    RULE_IMPLICIT_ALLOW_UUID,
                    ActionListType(simple_action='pass'),
                    '<>')

                # Deny any-vn to any-vn trffic
                this_vn_to_any_vn = self.add_acl_rule(
                    AddressType(virtual_network=self.name),
                    PortType(),
                    AddressType(virtual_network="any"),
                    PortType(),
                    "any",
                    RULE_IMPLICIT_DENY_UUID,
                    ActionListType("deny"),
                    '<>')
                acl_list.append(this_vn_to_provider_acl)
                acl_list.append(this_vn_to_any_vn)

            if self._manager._args.logical_routers_enabled:
                for rule in static_acl_entries.get_acl_rule():
                    src_address = \
                        copy.deepcopy(rule.match_condition.src_address)
                    dst_address = \
                        copy.deepcopy(rule.match_condition.dst_address)
                    if src_address.virtual_network:
                        src_address.subnet = None
                        src_address.subnet_list = []
                    if dst_address.virtual_network:
                        dst_address.subnet = None
                        dst_address.subnet_list = []

                    acl = self.add_acl_rule(
                        src_address, PortType(), dst_address, PortType(),
                        "any", rule.get_rule_uuid(), ActionListType("deny"),
                        rule.direction)

                    acl_list.append(acl)
                # end for rule

                # Add provider-network to any vn deny
                if self.is_provider_network:
                    provider_to_any_acl = self.add_acl_rule(
                        AddressType(virtual_network=self.name),
                        PortType(),
                        AddressType(virtual_network="any"),
                        PortType(),
                        "any",
                        RULE_IMPLICIT_DENY_UUID,
                        ActionListType("deny"),
                        '<>')
                    acl_list.append(provider_to_any_acl)
                else:
                    # If this VN is linked to a provider-network, add
                    # provider-vn <-> this-VN deny
                    for provider_vn in self.virtual_networks:
                        # add this vn to provider-network deny
                        this_vn_to_provider_acl = self.add_acl_rule(
                            AddressType(virtual_network=self.name),
                            PortType(),
                            AddressType(virtual_network=provider_vn),
                            PortType(),
                            "any",
                            RULE_IMPLICIT_DENY_UUID,
                            ActionListType("deny"),
                            '<>')
                        acl_list.append(this_vn_to_provider_acl)

                # Create any-vn to any-vn allow
                match = MatchConditionType(
                    "any", AddressType(virtual_network="any"), PortType(),
                    AddressType(virtual_network="any"), PortType())
                action = ActionListType("pass")
                acl = AclRuleType(match, action, RULE_IMPLICIT_ALLOW_UUID)
                acl_list.append(acl)
                acl_list.update_acl_entries(static_acl_entries)
            else:
                # Create any-vn to any-vn deny
                match = MatchConditionType(
                    "any", AddressType(virtual_network="any"), PortType(),
                    AddressType(virtual_network="any"), PortType())
                action = ActionListType("deny")
                acl = AclRuleType(match, action, RULE_IMPLICIT_DENY_UUID)
                acl_list.append(acl)
                acl_list.update_acl_entries(static_acl_entries)
            self.acl_rule_count = len(static_acl_entries.get_acl_rule())

        self.acl = _access_control_list_update(self.acl, self.obj.name,
                                               self.obj, static_acl_entries)
        self.dynamic_acl = _access_control_list_update(self.dynamic_acl,
                                                       'dynamic', self.obj,
                                                       dynamic_acl_entries)

        for vmi_name in self.virtual_machine_interfaces:
            vmi = ResourceBaseST.get_obj_type_map().get(
                'virtual_machine_interface').get(vmi_name)
            if (vmi and vmi.interface_mirror is not None and
                    vmi.interface_mirror.mirror_to is not None and
                    vmi.interface_mirror.mirror_to.analyzer_name is not None):
                vmi.process_analyzer()

        # This VN could be the VN for an analyzer interface. If so, we need
        # to create a link from all VNs containing a policy with that
        # analyzer
        for policy in list(ResourceBaseST.get_obj_type_map().get(
                'network_policy').values()):
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

        self.delete_inactive_service_chains(old_service_chains,
                                            self.service_chains)

        primary_ri = self.get_primary_routing_instance()
        if primary_ri:
            primary_ri.update_static_routes()
            old_rp_set = set(primary_ri.routing_policys.keys())
            new_rp_set = set(self.routing_policys.keys())
            for rp in new_rp_set - old_rp_set:
                rp_obj = ResourceBaseST.get_obj_type_map().get(
                    'routing_policy').get(rp)
                seq = self.routing_policys[rp].sequence
                rp_obj.add_routing_instance(primary_ri, seq)
                primary_ri.routing_policys[rp] = seq

            for rp in old_rp_set - new_rp_set:
                rp_obj = ResourceBaseST.get_obj_type_map().get(
                    'routing_policy').get(rp)
                rp_obj.delete_routing_instance(primary_ri)
                del primary_ri.routing_policys[rp]

        self.update_pnf_presence()
        self.check_multi_policy_service_chain_status()
        for ri_name in self.routing_instances:
            ri = ResourceBaseST.get_obj_type_map().get(
                'routing_instance').get(ri_name)
            if ri:
                ri.update_routing_policy_and_aggregates()
        self.uve_send()
    # end evaluate

    def get_prefixes(self, ip_version):
        prefixes = []
        for ipam in list(self.ipams.values()):
            for ipam_subnet in ipam.ipam_subnets:
                # LP #1717838
                # IPAMs with subnet method = FLAT do not have subnet
                # Skip exporting prefixes of FLAT subnets
                if ipam_subnet.subnet:
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
            sandesh.PropList('bgpvpn_rt_list', ', '.join(self.bgpvpn_rt_list)),
            sandesh.PropList('bgpvpn_import_rt_list',
                             ', '.join(self.bgpvpn_import_rt_list)),
            sandesh.PropList('bgpvpn_export_rt_list',
                             ', '.join(self.bgpvpn_export_rt_list)),
        ])
        return resp
    # end handle_st_object_req

    def get_gateway(self, address):
        """Return the default gateway of the network.

        to which the 'address' belongs.
        """
        for ipam in list(self.ipams.values()):
            for ipam_subnet in ipam.ipam_subnets:
                network = IPNetwork('%s/%s' % (ipam_subnet.subnet.ip_prefix,
                                    ipam_subnet.subnet.ip_prefix_len))
                if address in network:
                    return ipam_subnet.default_gateway
    # end get_gateway
# end class VirtualNetworkST


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
        l_subnets = [IPNetwork('%s/%d' % (s.ip_prefix, s.ip_prefix_len))
                     for s in l_subnets]
        r_subnets = rhs.subnet_list or []
        if rhs.subnet:
            r_subnets.append(rhs.subnet)
        r_subnets = [IPNetwork('%s/%d' % (s.ip_prefix, s.ip_prefix_len))
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
                    self._address_is_subset(lhs.src_address,
                                            rhs.src_address) and
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
