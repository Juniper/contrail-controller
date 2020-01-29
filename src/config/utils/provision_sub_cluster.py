#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

# python provision_sub_cluster.py --sub_cluster_name issu-vm3
# --api_server_ip 10.87.64.55 --api_server_port 8082 --oper add
# --sub_cluster_asn 64513 --openstack_ip 10.87.64.55 --admin_user admin
# --admin_password c0ntrail123 --admin_tenant_name admin

# python provision_control.py --host_name issu-vm4 --host_ip 10.87.64.54
# --api_server_ip 10.87.64.55 --api_server_port 8082 --admin_user admin
# --admin_password c0ntrail123 --admin_tenant_name admin
# --no_ibgp_auto_mesh  --sub_cluster_name issu-vm4 --router_asn 64513 --oper add

# python provision_vrouter.py --host_name issu-vm2 --host_ip 10.87.64.52
# --api_server_ip 10.87.64.55 --api_server_port 8082  --admin_user admin
# --admin_password c0ntrail123 --admin_tenant_name admin
# --sub_cluster_name issu-vm4 --oper add --openstack_ip 10.87.64.55

from __future__ import print_function
from future import standard_library
standard_library.install_aliases()
from builtins import object
import sys
import time
import argparse
import configparser

from vnc_api.vnc_api import *
from cfgm_common.exceptions import *


class SubClusterProvisioner(object):

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

        if self._args.oper == 'add':
            self.add_sub_cluster()
        elif self._args.oper == 'del':
            self.del_sub_cluster()
        else:
            print("Unknown operation %s. Only 'add' and 'del' supported"\
                % (self._args.oper))

    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python provision_sub_cluster.py --sub_cluster_name foo
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --api_server_use_ssl False
                                        --sub_cluster_asn 1-65535
                                        --oper <add | del>
                                        [--sub_cluster_id 1-4294967295]
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
            'sub_cluster_asn': '64513',
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
            "--sub_cluster_name", help="name of sub cluster", required=True)
        parser.add_argument(
            "--api_server_ip", help="IP address of api server", required=True)
        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
        parser.add_argument(
            "--oper", default='add',
            help="Provision operation to be done(add or del)")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenant name for keystone admin user")
        parser.add_argument(
            "--openstack_ip", help="Openstack IP for authentication")
        parser.add_argument(
            "--sub_cluster_asn", help="Sub cluster's ASN")
        parser.add_argument(
            "--sub_cluster_id",
            help=("Sub cluster's ID between 1-4294967295 depending of the "
                  "size of the global ASN. If not provided, an ID is "
                  "automatically allocated"))
        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

    def add_sub_cluster(self):
        sub_cluster_obj = SubCluster(
            self._args.sub_cluster_name,
            sub_cluster_asn=self._args.sub_cluster_asn,
            sub_cluster_id=self._args.sub_cluster_id)
        sub_cluster_exists = True
        try:
            sub_cluster_obj = self._vnc_lib.sub_cluster_read(
                fq_name=sub_cluster_obj.get_fq_name())
        except NoIdError:
            sub_cluster_exists = False

        if sub_cluster_exists:
            self._vnc_lib.sub_cluster_update(sub_cluster_obj)
        else:
            self._vnc_lib.sub_cluster_create(sub_cluster_obj)

    # end add_sub_cluster

    def del_sub_cluster(self):
        sub_cluster_obj = SubCluster(self._args.sub_cluster_name)
        self._vnc_lib.sub_cluster_delete(
            fq_name=sub_cluster_obj.get_fq_name())
    # end del_sub_cluster

# end class SubClusterProvisioner


def main(args_str=None):
    SubClusterProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
