#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import argparse
import ConfigParser

import json
import copy
from netaddr import IPNetwork

from vnc_api.vnc_api import *


def get_ip(ip_w_pfx):
    return str(IPNetwork(ip_w_pfx).ip)
# end get_ip


class VncProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._prov_info = self._read_provision_data()
        prov_info = self._prov_info

        self._bgp_addr_fams = AddressFamilies(['inet-vpn'])
        self._bgp_sess_attrs = [
            BgpSessionAttributes(address_families=self._bgp_addr_fams)]
        self._bgp_sessions = [BgpSession(attributes=self._bgp_sess_attrs)]
        self._bgp_peering_attrs = BgpPeeringAttributes(
            session=self._bgp_sessions)

        self._vnc_lib = VncApi(self._args.admin_user,
                               self._args.admin_password,
                               self._args.admin_tenant_name,
                               self._args.api_server_ip,
                               self._args.api_server_port, '/')
        vnc_lib = self._vnc_lib

        gsc_obj = vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])

        gsc_obj.set_autonomous_system(prov_info['bgp-asn'])
        gsc_obj.set_ibgp_auto_mesh(True)
        vnc_lib.global_system_config_update(gsc_obj)
        self._global_system_config_obj = gsc_obj

        # TODO pick fqname hardcode from common
        rt_inst_obj = vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        self._fab_rt_inst_obj = rt_inst_obj

        vrouter_hosts = []
        for host in prov_info['hosts']:
            for role in host['roles']:
                if role['type'] == 'bgp':
                    param = role['params']
                    self.add_bgp_router(host['name'], host['ip'])
                elif role['type'] == 'compute':
                    vrouter_hosts.append((host, role))

        for host, role in vrouter_hosts:
            param = role['params']
            self.add_vrouter(host['name'], host['ip'], param['bgp'])

    # end __init__

    def add_vrouter(self, name, ip, bgps):
        vnc_lib = self._vnc_lib
        gsc_obj = self._global_system_config_obj

        vrouter_obj = VirtualRouter(
            name, gsc_obj, virtual_router_ip_address=ip)
        for bgp in bgps:
            bgp_router_fq_name = copy.deepcopy(
                self._fab_rt_inst_obj.get_fq_name())
            bgp_router_fq_name.append(bgp)
            bgp_router_obj = vnc_lib.bgp_router_read(
                fq_name=bgp_router_fq_name)
            vrouter_obj.add_bgp_router(bgp_router_obj)

        vnc_lib.virtual_router_create(vrouter_obj)

    # end add_vrouter

    def add_bgp_router(self, name, ip):
        vnc_lib = self._vnc_lib

        gsc_obj = self._global_system_config_obj
        router_params = BgpRouterParams(
            autonomous_system=gsc_obj.get_autonomous_system(),
            identifier=get_ip(ip), address=get_ip(ip), port=179,
            address_families=self._bgp_addr_fams)

        bgp_router_obj = BgpRouter(name, self._fab_rt_inst_obj,
                                   bgp_router_parameters=router_params)

        cur_id = vnc_lib.bgp_router_create(bgp_router_obj)
        cur_obj = vnc_lib.bgp_router_read(id=cur_id)

        # full-mesh with existing bgp routers
        fq_name = self._fab_rt_inst_obj.get_fq_name()
        bgp_router_list = vnc_lib.bgp_routers_list(
            routing_instance_fq_name=fq_name)
        bgp_router_ids = [bgp_dict['uuid']
                          for bgp_dict in bgp_router_list['bgp-routers']]
        bgp_router_objs = []
        for id in bgp_router_ids:
            bgp_router_objs.append(vnc_lib.bgp_router_read(id=id))

        for other_obj in bgp_router_objs:
            if other_obj.uuid == cur_id:
                continue
            other_obj.add_bgp_router(cur_obj, self._bgp_peering_attrs)
            vnc_lib.bgp_router_update(other_obj)
            cur_obj.add_bgp_router(other_obj, self._bgp_peering_attrs)
        vnc_lib.bgp_router_update(cur_obj)

    # end add_bgp_router

    def _parse_args(self, args_str):
        '''
        Eg. python provision.py --prov_data_file provision.json
                                --api_server_ip 127.0.0.1
                                --api_server_port 8082
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'prov_data_file': 'provision.json',
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
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
        defaults.update(ksopts)
        parser.set_defaults(**defaults)

        parser.add_argument(
            "--prov_data_file", help="File name of provision data in json")
        parser.add_argument(
            "--api_server_ip", help="IP address of api server")
        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user")

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

    def _read_provision_data(self):
        prov_file = open(self._args.prov_data_file, 'r')
        prov_data = prov_file.read()

        return json.loads(prov_data)
    # end _read_provision_data

# end class VncProvisioner


def main(args_str=None):
    VncProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
