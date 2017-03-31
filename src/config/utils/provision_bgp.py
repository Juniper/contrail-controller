#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

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
                 api_server_use_ssl=False, use_admin_api=False):
        self._admin_user = user
        self._admin_password = password
        self._admin_tenant_name = tenant
        self._api_server_ip = api_server_ip
        self._api_server_port = api_server_port
        self._api_server_use_ssl = api_server_use_ssl
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
                       router_asn, address_families=[], md5=None):
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
            port=179, address_families=bgp_addr_fams)

        bgp_router_obj = BgpRouter(router_name, rt_inst_obj,
                                   bgp_router_parameters=router_params)

        # Return early with a log if it already exists
        try:
            fq_name = bgp_router_obj.get_fq_name()
            existing_obj = vnc_lib.bgp_router_read(fq_name=fq_name)
            if md5:
                bgp_params = existing_obj.get_bgp_router_parameters()
                # set md5
                print "Setting md5 on the existing uuid"
                md5 = {'key_items': [ { 'key': md5 ,"key_id":0 } ], "key_type":"md5"}
                bgp_params.set_auth_data(md5)
                existing_obj.set_bgp_router_parameters(bgp_params)
                vnc_lib.bgp_router_update(existing_obj)
            print ("BGP Router " + pformat(fq_name) +
                   " already exists with uuid " + existing_obj.uuid)
            return
        except NoIdError:
            pass

        cur_id = vnc_lib.bgp_router_create(bgp_router_obj)
        cur_obj = vnc_lib.bgp_router_read(id=cur_id)

        # full-mesh with existing bgp routers
        fq_name = rt_inst_obj.get_fq_name()
        bgp_router_list = vnc_lib.bgp_routers_list(parent_fq_name=fq_name)
        bgp_router_ids = [bgp_dict['uuid']
                          for bgp_dict in bgp_router_list['bgp-routers']]
        bgp_router_objs = []
        for id in bgp_router_ids:
            bgp_router_objs.append(vnc_lib.bgp_router_read(id=id))

        for other_obj in bgp_router_objs:
            if other_obj.uuid == cur_id:
                continue

            cur_obj.add_bgp_router(other_obj, bgp_peering_attrs)
        if md5:
            md5 = {'key_items': [ { 'key': md5 ,"key_id":0 } ], "key_type":"md5"}
            rparams = cur_obj.bgp_router_parameters
            rparams.set_auth_data(md5)
            cur_obj.set_bgp_router_parameters(rparams)
        vnc_lib.bgp_router_update(cur_obj)
    # end add_bgp_router

    def del_bgp_router(self, router_name):
        vnc_lib = self._vnc_lib

        rt_inst_obj = self._get_rt_inst_obj()

        fq_name = rt_inst_obj.get_fq_name() + [router_name]
        cur_obj = vnc_lib.bgp_router_read(fq_name=fq_name)

        # remove full-mesh with existing bgp routers
        fq_name = rt_inst_obj.get_fq_name()
        bgp_router_list = vnc_lib.bgp_routers_list(parent_fq_name=fq_name)
        bgp_router_ids = [bgp_dict['uuid']
                          for bgp_dict in bgp_router_list['bgp-routers']]
        bgp_router_objs = []
        for id in bgp_router_ids:
            bgp_router_objs.append(vnc_lib.bgp_router_read(id=id))

        for other_obj in bgp_router_objs:
            if other_obj.uuid == cur_obj.uuid:
                # our refs will be dropped on delete further down
                continue

            other_obj.del_bgp_router(cur_obj)

        vnc_lib.bgp_router_delete(id=cur_obj.uuid)
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
            print "%s not configured for VN %s" % (rtgt_val,
                                                   rt_inst_fq_name[:-1])
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
