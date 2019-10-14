#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from builtins import str
from builtins import object
import json
import copy
from netaddr import IPNetwork
from pprint import pformat

from vnc_api.vnc_api import *
from vnc_admin_api import VncApiAdmin


def get_ip(ip_w_pfx):
    return str(IPNetwork(ip_w_pfx).ip)
# end get_ip


class BgpProvisioner(object):

    def __init__(self, user, password, tenant, api_server_ip, api_server_port,
                 api_server_use_ssl=False, use_admin_api=False,
                 sub_cluster_name=None, peer_list=None):
        self._admin_user = user
        self._admin_password = password
        self._admin_tenant_name = tenant
        self._api_server_ip = api_server_ip
        self._api_server_port = api_server_port
        self._api_server_use_ssl = api_server_use_ssl
        self._sub_cluster_name = sub_cluster_name
        self._peer_list = peer_list
        self._vnc_lib = VncApiAdmin(
            use_admin_api, self._admin_user, self._admin_password,
            self._admin_tenant_name,
            self._api_server_ip,
            self._api_server_port, '/',
            api_server_use_ssl=self._api_server_use_ssl)
    # end __init__

    def _get_rt_inst_obj(self):
        vnc_lib = self._vnc_lib

        # TODO pick fqname hardcode from common
        rt_inst_obj = vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])

        return rt_inst_obj
    # end _get_rt_inst_obj

    def add_bgp_router(self, router_type, router_name, router_ip,
                       router_asn, address_families=[], md5=None,
                       local_asn=None, port=179):
        if not address_families:
            address_families = ['route-target', 'inet-vpn', 'e-vpn', 'erm-vpn',
                                'inet6-vpn']
            if router_type != 'control-node':
                address_families.remove('erm-vpn')

        if router_type != 'control-node':
            if 'erm-vpn' in address_families:
                raise RuntimeError("Only contrail bgp routers can support "
                                   "family 'erm-vpn'")

        bgp_addr_fams = AddressFamilies(address_families)

        bgp_sess_attrs = [
            BgpSessionAttributes(address_families=bgp_addr_fams)]
        bgp_sessions = [BgpSession(attributes=bgp_sess_attrs)]
        bgp_peering_attrs = BgpPeeringAttributes(session=bgp_sessions)

        rt_inst_obj = self._get_rt_inst_obj()

        vnc_lib = self._vnc_lib

        if router_type == 'control-node':
            vendor = 'contrail'
        elif router_type == 'router':
            vendor = 'mx'
        else:
            vendor = 'unknown'

        router_params = BgpRouterParams(router_type=router_type,
            vendor=vendor, autonomous_system=int(router_asn),
            identifier=get_ip(router_ip),
            address=get_ip(router_ip),
            port=port, address_families=bgp_addr_fams)

        if md5:
            md5 = {'key_items': [ { 'key': md5 ,"key_id":0 } ], "key_type":"md5"}
            router_params.set_auth_data(md5)

        if local_asn:
            local_asn = int(local_asn)
            if local_asn <= 0 or local_asn > 65535:
                raise argparse.ArgumentTypeError("local_asn %s must be in range (1..65535)" % local_asn)
            router_params.set_local_autonomous_system(local_asn)

        bgp_router_obj = BgpRouter(router_name, rt_inst_obj,
                                   bgp_router_parameters=router_params)
        bgp_router_fq_name = bgp_router_obj.get_fq_name()
        peer_list = self._peer_list
        fqname_peer_list = None
        fqname_peer_set = None
        if peer_list is not None:
            fqname_peer_list = []
            fqname_peer_set = set()
            for peer in peer_list:
                peer_router_obj = BgpRouter(peer, rt_inst_obj)
                try:
                    vnc_lib.bgp_router_create(peer_router_obj)
                except RefsExistError as e:
                    pass
                finally:
                    fqname_peer_list.append(peer_router_obj.get_fq_name())
                    fqname_peer_set.add(tuple(peer_router_obj.get_fq_name()))
        try:
            # full-mesh with existing bgp routers
            if self._sub_cluster_name:
                sub_cluster_obj = SubCluster(self._sub_cluster_name)
                try:
                    sub_cluster_obj = self._vnc_lib.sub_cluster_read(
                    fq_name=sub_cluster_obj.get_fq_name())
                except NoIdError:
                    raise RuntimeError("Sub cluster to be provisioned first")
                bgp_router_obj.add_sub_cluster(sub_cluster_obj)
            if fqname_peer_list:
                bgp_router_obj.set_bgp_router_list(fqname_peer_list,
                    [bgp_peering_attrs]*len(fqname_peer_list))
            vnc_lib.bgp_router_create(bgp_router_obj)
        except RefsExistError as e:
            print ("BGP Router " + pformat(bgp_router_fq_name) +
                   " already exists " + str(e))
            cur_obj = vnc_lib.bgp_router_read(
                    fq_name=bgp_router_fq_name,
                    fields=['global_system_config_back_refs'])
            changed = False
            if cur_obj.bgp_router_parameters != router_params:
                cur_obj.set_bgp_router_parameters(router_params)
                changed = True
            cur_bgp_router_set = {tuple(x['to'])
                                   for x in cur_obj.get_bgp_router_refs() or []}
            if peer_list is not None and fqname_peer_set != cur_bgp_router_set:
                cur_obj.set_bgp_router_list(fqname_peer_list,
                    [bgp_peering_attrs]*len(fqname_peer_list))
                changed = True
            if self._sub_cluster_name and not cur_obj.get_sub_cluster_refs():
                cur_obj.add_sub_cluster(sub_cluster_obj)
                changed = True
            if changed:
                vnc_lib.bgp_router_update(cur_obj)
            bgp_router_obj = cur_obj

        if (router_type == 'control-node' and
                not bgp_router_obj.get_global_system_config_back_refs()):
            gsc_obj = vnc_lib.global_system_config_read(
                        fq_name=['default-global-system-config'])
            gsc_obj.add_bgp_router(bgp_router_obj)
            vnc_lib.ref_relax_for_delete(gsc_obj.uuid, bgp_router_obj.uuid)
            vnc_lib.global_system_config_update(gsc_obj)

    # end add_bgp_router

    def del_bgp_router(self, router_name):
        rt_inst_obj = self._get_rt_inst_obj()
        fq_name = rt_inst_obj.get_fq_name() + [router_name]
        self._vnc_lib.bgp_router_delete(fq_name=fq_name)
    # end del_bgp_router

    def add_route_target(self, rt_inst_fq_name, router_asn,
                         route_target_number):
        vnc_lib = self._vnc_lib

        rtgt_val = "target:%s:%s" % (router_asn, route_target_number)

        net_obj = vnc_lib.virtual_network_read(fq_name=rt_inst_fq_name[:-1])
        route_targets = net_obj.get_route_target_list()
        if route_targets:
            route_targets.add_route_target(rtgt_val)
        else:
            route_targets = RouteTargetList([rtgt_val])
        net_obj.set_route_target_list(route_targets)

        vnc_lib.virtual_network_update(net_obj)
    # end add_route_target

    def del_route_target(self, rt_inst_fq_name, router_asn,
                         route_target_number):
        vnc_lib = self._vnc_lib

        rtgt_val = "target:%s:%s" % (router_asn, route_target_number)
        net_obj = vnc_lib.virtual_network_read(fq_name=rt_inst_fq_name[:-1])

        if rtgt_val not in net_obj.get_route_target_list().get_route_target():
            print("%s not configured for VN %s" % (rtgt_val,
                                                   rt_inst_fq_name[:-1]))
            return

        route_targets = net_obj.get_route_target_list()
        route_targets.delete_route_target(rtgt_val)
        if route_targets.get_route_target():
            net_obj.set_route_target_list(route_targets)
        else:
            net_obj.set_route_target_list(None)
        vnc_lib.virtual_network_update(net_obj)
    # end del_route_target

# end class BgpProvisioner
