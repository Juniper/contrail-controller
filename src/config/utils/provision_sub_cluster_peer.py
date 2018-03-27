#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import time
import argparse
import ConfigParser

from vnc_api.vnc_api import *
from cfgm_common.exceptions import *


class SubClusterPeer(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        connected = False
        tries = 0
        while not connected:
            try:
                self._vnc_lib = VncApi(
                    self._args.admin_user, self._args.admin_password,
                    self._args.admin_tenant_name,
                    self._args.api_server_ip,
                    self._args.api_server_port, '/',
                    auth_host=self._args.openstack_ip,
                    api_server_use_ssl=self._args.api_server_use_ssl)
                connected = True
            except ResourceExhaustionError: # haproxy throws 503
                if tries < 10:
                    tries += 1
                    time.sleep(3)
                else:
                    raise
        self.peer_controls_in_sub_clusters()

    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python provision_sub_cluster_peer.py 
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --api_server_use_ssl False
                                        --sub_cluster_name None 
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
            'api_server_use_ssl': False,
            'oper': 'add',
            'sub_cluster_name': None,
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
            "--api_server_ip", help="IP address of api server", required=True)
        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenant name for keystone admin user")
        parser.add_argument(
            "--openstack_ip", help="Openstack IP for authentication")
        parser.add_argument(
            "--sub_cluster_name", help="Sub cluster name")
        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

    def peer_controls_in_sub_cluster(self, sub_cluster_obj):
        if sub_cluster_obj == None:
            return
        refs = sub_cluster_obj.get_bgp_router_back_refs()
        if refs:
            bgp_router_objs = ([self._vnc_lib.bgp_router_read(id=ref['uuid'])
                          for ref in refs])
            while(bgp_router_objs):
                current_obj = bgp_router_objs.pop()
                bgp_addr_fams=current_obj.get_bgp_router_parameters().address_families
                bgp_sess_attrs = [BgpSessionAttributes(address_families=bgp_addr_fams)]
                bgp_sessions = [BgpSession(attributes=bgp_sess_attrs)]
                bgp_peering_attrs = BgpPeeringAttributes(session=bgp_sessions)
                changed = False
                for bgp_obj in bgp_router_objs:
                    print "Peering %s %s" %(current_obj.fq_name, bgp_obj.fq_name)
                    current_obj.add_bgp_router(bgp_obj, bgp_peering_attrs)
                    changed = True
                if changed:
                    self._vnc_lib.bgp_router_update(current_obj)

    def peer_controls_in_sub_clusters(self):
        # Peer control nodes of this sub cluster
        if self._args.sub_cluster_name:
            sub_cluster = SubCluster(self._args.sub_cluster_name)
            sub_cluster_obj = self._vnc_lib.sub_cluster_read(
                    fq_name=sub_cluster.get_fq_name())
            self.peer_controls_in_sub_cluster(sub_cluster_obj)
        else: 
        # Peer all Subclusters in the system
            sub_cluster_list = self._vnc_lib.sub_clusters_list()
            for sub_cluster in sub_cluster_list['sub-clusters']:
                sub_cluster_obj = self._vnc_lib.sub_cluster_read(
                                             fq_name=sub_cluster['fq_name'])
                self.peer_controls_in_sub_cluster(sub_cluster_obj)

def main(args_str=None):
    SubClusterPeer(args_str)
# end main

if __name__ == "__main__":
    main()
