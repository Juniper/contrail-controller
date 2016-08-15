#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import argparse
import ConfigParser

from provision_bgp import BgpProvisioner
from vnc_api.vnc_api import *


class ControlProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        if self._args.router_asn and not self._args.oper:
            self._vnc_lib = VncApi(
            self._args.admin_user, self._args.admin_password, self._args.admin_tenant_name,
            self._args.api_server_ip,
            self._args.api_server_port, '/',
            api_server_use_ssl=self._args.api_server_use_ssl)

            # Update global system config also with this ASN
            gsc_obj = self._vnc_lib.global_system_config_read(
                  fq_name=['default-global-system-config'])
            gsc_obj.set_autonomous_system(self._args.router_asn)
            if self._args.ibgp_auto_mesh is not None:
                gsc_obj.set_ibgp_auto_mesh(self._args.ibgp_auto_mesh)

            gr_params = GracefulRestartType()
            gr_update = False
            if self._args.graceful_restart_time:
                gr_params.set_graceful_restart_time(
                    int(self._args.graceful_restart_time))
                gr_update = True
            if self._args.long_lived_graceful_restart_time:
                gr_params.set_long_lived_graceful_restart_time(
                    int(self._args.long_lived_graceful_restart_time))
                gr_update = True

            if gr_update:
                gsc_obj.set_graceful_restart_params(gr_params)

            self._vnc_lib.global_system_config_update(gsc_obj)
            return

        bp_obj = BgpProvisioner(
            self._args.admin_user, self._args.admin_password,
            self._args.admin_tenant_name,
            self._args.api_server_ip, self._args.api_server_port,
            api_server_use_ssl=self._args.api_server_use_ssl)
        if self._args.oper == 'add':
            bp_obj.add_bgp_router('contrail', self._args.host_name,
                                  self._args.host_ip, self._args.router_asn,
                                  self._args.address_families, self._args.md5)
        elif self._args.oper == 'del':
            bp_obj.del_bgp_router(self._args.host_name)
        else:
            print "Unknown operation %s. Only 'add' and 'del' supported"\
                % (self._args.oper)

    # end __init__

    def gr_time_type(self, value):
        time = int(value)
        if time < 0 or time > 4095:
            raise argparse.ArgumentTypeError("graceful_restart_time %s must be in range (0..4095)" % value)
        return time

    def llgr_time_type(self, value):
        time = int(value)
        if time < 0 or time > 16777215:
            raise argparse.ArgumentTypeError("long_lived_graceful_restart_time %s must be in range (0..16777215)" % value)
        return time

    def _parse_args(self, args_str):
        '''
        Eg. python provision_control.py --host_name a3s30.contrail.juniper.net
                                        --host_ip 10.1.1.1
                                        --router_asn 64512
                                        --ibgp_auto_mesh|--no_ibgp_auto_mesh
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --api_server_use_ssl False
                                        --oper <add | del>
                                        --md5 <key value>|None(optional)
                                        --graceful-restart-time 100
                                        --long-lived-graceful-restart-time 100

        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'router_asn': '64512',
            'ibgp_auto_mesh': None,
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
            'api_server_use_ssl': False,
            'oper': None,
            'admin_user': None,
            'admin_password': None,
            'admin_tenant_name': None,
            'md5' : None,
            'graceful-restart-time': None,
            'long_lived_graceful_restart_time': None,
        }

        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read([args.conf_file])
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

        parser.add_argument(
            "--host_name", help="hostname name of control-node")
        parser.add_argument("--host_ip", help="IP address of control-node")
        parser.add_argument(
            "--router_asn", help="AS Number the control-node is in", required=True)
        parser.add_argument(
            "--address_families", help="Address family list",
            choices=["route-target", "inet-vpn", "e-vpn", "erm-vpn", "inet6-vpn"],
            nargs="*", default=[])
        parser.add_argument(
            "--md5", help="Md5 config for the node")
        parser.add_argument(
            "--ibgp_auto_mesh", help="Create iBGP mesh automatically", dest='ibgp_auto_mesh', action='store_true')
        parser.add_argument(
            "--no_ibgp_auto_mesh", help="Don't create iBGP mesh automatically", dest='ibgp_auto_mesh', action='store_false')
        parser.add_argument(
            "--api_server_ip", help="IP address of api server", required=True)
        parser.add_argument("--api_server_port", help="Port of api server", required=True)
        parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
        parser.add_argument(
            "--oper",
            help="Provision operation to be done(add or del)")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user")
        parser.add_argument(
            "--graceful_restart_time",
            help="Graceful Restart Time in seconds (0..4095)",
            type=self.gr_time_type,
            required=False)
        parser.add_argument(
            "--long_lived_graceful_restart_time",
            help="Long Lived Graceful Restart Time in seconds (0..16777215)",
            type=self.llgr_time_type,
            required=False)

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

# end class ControlProvisioner


def main(args_str=None):
    ControlProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
