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


class ISSUContrailPostProvisioner(object):

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

        gsc_obj = self._vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])
        self._global_system_config_obj = gsc_obj

        self.del_nodes()
        self.add_nodes()
    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python provision_post_issu.py --host_name a3s30.contrail.juniper.net
                                        --host_ip 10.1.1.1
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --api_server_use_ssl False
                                        --oper <add | del>
        '''

        defaults = {
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
            'api_server_use_ssl': False,
            'oper': 'add',
        }
        ksopts = {
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'default-domain'
        }

        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        conf_parser = argparse.ArgumentParser(add_help=False)
        conf_parser.add_argument("-c", "--conf_file", action='append',
            help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read(args.conf_file)
            defaults.update(dict(config.items("DEFAULTS")))

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
        parser.set_defaults(**defaults)

        group.add_argument(
            "--api_server_ip", help="IP address of api server")
        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user")
        parser.add_argument(
            "--openstack_ip", help="IP address of openstack node")
        args_obj, remaining_argv = parser.parse_known_args(remaining_argv)
        if args.conf_file:
            args_obj.config_section = config

        if type(args_obj.db_host_info) is str:
            json_string=args_obj.db_host_info.replace("'", "\"")
            args_obj.db_host_info=\
                json.loads(json_string)

        if type(args_obj.config_host_info) is str:
            json_string=args_obj.config_host_info.replace("'", "\"")
            args_obj.config_host_info=\
                json.loads(json_string)

        if type(args_obj.analytics_host_info) is str:
            json_string=args_obj.analytics_host_info.replace("'", "\"")
            args_obj.analytics_host_info=\
                json.loads(json_string)

        if type(args_obj.control_host_info) is str:
            json_string=args_obj.control_host_info.replace("'", "\"")
            args_obj.control_host_info=\
                json.loads(json_string)

        self._args = args_obj
    # end _parse_args

    def add_nodes(self):
        gsc_obj = self._global_system_config_obj

        for k,v in self._args.db_host_info.items():
            database_node_obj = DatabaseNode(
                v, gsc_obj,
                database_node_ip_address=k)
            database_node_exists = True
            try:
                database_node_obj = self._vnc_lib.database_node_read(
                    fq_name=database_node_obj.get_fq_name())
            except NoIdError:
                database_node_exists = False

            if database_node_exists:
                print "updated"
                self._vnc_lib.database_node_update(database_node_obj)
            else:
                print "created"
                self._vnc_lib.database_node_create(database_node_obj)

        for k,v in self._args.config_host_info.items():
            config_node_obj = ConfigNode(
                v, gsc_obj,
                config_node_ip_address=k)
            config_node_exists = True
            try:
                config_node_obj = self._vnc_lib.config_node_read(
                    fq_name=config_node_obj.get_fq_name())
            except NoIdError:
                config_node_exists = False

            if config_node_exists:
                print "updated config"
                self._vnc_lib.config_node_update(config_node_obj)
            else:
                print "created config"
                self._vnc_lib.config_node_create(config_node_obj)

        for k,v in self._args.analytics_host_info.items():
            analytics_node_obj = AnalyticsNode(
                v, gsc_obj,
                analytics_node_ip_address=k)
            analytics_node_exists = True
            try:
                analytics_node_obj = self._vnc_lib.analytics_node_read(
                    fq_name=analytics_node_obj.get_fq_name())
            except NoIdError:
                analytics_node_exists = False

            if analytics_node_exists:
                print "updated analytics"
                self._vnc_lib.analytics_node_update(analytics_node_obj)
            else:
                print "created analytics"
                self._vnc_lib.analytics_node_create(analytics_node_obj)


    # end add_node

    def del_nodes(self):
        # Delete old database node
        node_list = self._vnc_lib.database_nodes_list()
        node_list_values = node_list.values()
        db_name_list=[]
        for k,v in self._args.db_host_info.items():
            db_name_list.append(v)
        for node_list_value in node_list_values[0]:
            if node_list_value['fq_name'][1] in db_name_list:
                continue
            print "deleting %s" %(node_list_value['fq_name'])
            self._vnc_lib.database_node_delete(
             fq_name=node_list_value['fq_name'])

        # Delete old analytics node
        node_list = self._vnc_lib.analytics_nodes_list()
        node_list_values = node_list.values()
        analytics_name_list=[]
        for k,v in self._args.analytics_host_info.items():
            analytics_name_list.append(v)
        for node_list_value in node_list_values[0]:
            if node_list_value['fq_name'][1] in analytics_name_list:
                continue;
            print "deleting %s" %(node_list_value['fq_name'])
            self._vnc_lib.analytics_node_delete(
             fq_name=node_list_value['fq_name'])

        # Delete old config node
        node_list = self._vnc_lib.config_nodes_list()
        node_list_values = node_list.values()
        config_name_list=[]
        for k,v in self._args.config_host_info.items():
            config_name_list.append(v)
        for node_list_value in node_list_values[0]:
            if node_list_value['fq_name'][1] in config_name_list:
                continue;
            print "deleting %s" %(node_list_value['fq_name'])
            self._vnc_lib.config_node_delete(
             fq_name=node_list_value['fq_name'])

        # Delete old control node
        node_list = self._vnc_lib.bgp_routers_list()
        node_list_values = node_list.values()
        control_name_list=[]
        for k,v in self._args.control_host_info.items():
            control_name_list.append(v)
        for node_list_value in node_list_values[0]:
            router_info = self._vnc_lib.bgp_router_read(id=node_list_value['uuid'])
            print "control name lists %s " %(control_name_list)
            if node_list_value['fq_name'][4] in control_name_list or \
               router_info.bgp_router_parameters.router_type != 'control-node':
                continue;
            print "deleting %s %s" %(node_list_value['fq_name'][4],
                    router_info.bgp_router_parameters.router_type)
            self._vnc_lib.bgp_router_delete(id=node_list_value['uuid'])

    # end del_node

# end class ISSUContrailPostProvisioner


def main(args_str=None):
    ISSUContrailPostProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
