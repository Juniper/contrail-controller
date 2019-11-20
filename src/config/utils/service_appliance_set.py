#!/usr/bin/python
#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from future import standard_library
standard_library.install_aliases()
from builtins import object
import sys
import time
import argparse
import configparser
import json

from vnc_api.vnc_api import *
from cfgm_common.exceptions import *

class SASProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = '|'.join(sys.argv[1:])
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
                    auth_host=self._args.openstack_ip)
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

        if self._args.oper == 'add':
            self.add_sas()
        elif self._args.oper == 'del':
            self.del_sas()
        else:
            print("Unknown operation %s. Only 'add' and 'del' supported"\
                % (self._args.oper))

    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python service_appliance_set.py --name f5
                                        --driver python_module
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --oper <add | del>
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split('|'))

        defaults = {
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
            'oper': 'add',
        }
        ksopts = {
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'default-domain'
        }

        if args.conf_file:
            config = configparser.SafeConfigParser()
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
            "--name", help="name of service appliance set", required=True)
        parser.add_argument("--driver", help="python module of the lbaas provider")
        parser.add_argument("--properties",
                help="JSON dictionary of config params for the lbaas provider",
                type=json.loads, default=json.loads("{}"))
        parser.add_argument(
            "--api_server_ip", help="IP address of api server", required=True)
        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument(
            "--oper", default='add',
            help="Provision operation to be done(add or del)")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user")
        parser.add_argument(
            "--openstack_ip", help="IP address of openstack node")
        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

    def add_sas(self):
        gsc_obj = self._global_system_config_obj

        default_gsc_name = "default-global-system-config"
        default_gsc_fq_name = [default_gsc_name]
        sa_set_fq_name = [default_gsc_name, self._args.name]

        sas_obj = ServiceApplianceSet(self._args.name, gsc_obj)

        try:
            sa_set_obj = self._vnc_lib.service_appliance_set_read(fq_name=sa_set_fq_name)
            return
        except NoIdError:
            pass
        sas_obj.set_service_appliance_driver(self._args.driver)
        kvp_array = []
        for r,c in self._args.properties.items():
            kvp = KeyValuePair(r,c)
            kvp_array.append(kvp)
        kvps = KeyValuePairs()
        if kvp_array:
            kvps.set_key_value_pair(kvp_array)
        sas_obj.set_service_appliance_set_properties(kvps)
        sa_set_uuid = self._vnc_lib.service_appliance_set_create(sas_obj)
    # end add_sas

    def del_sas(self):
        gsc_obj = self._global_system_config_obj

        default_gsc_name = "default-global-system-config"
        default_gsc_fq_name = [default_gsc_name]
        sa_set_fq_name = [default_gsc_name, self._args.name]

        sa_set_obj = self._vnc_lib.service_appliance_set_delete(fq_name=sa_set_fq_name)
    # end del_sas

# end class SASProvisioner


def main(args_str=None):
    SASProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
