#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from __future__ import print_function
from future import standard_library
standard_library.install_aliases()
from builtins import str
import sys
import argparse
from six.moves import configparser


from vnc_api.exceptions import NoIdError
from provision_bgp import BgpProvisioner
from vnc_api.vnc_api import *
from vnc_admin_api import VncApiAdmin


class ControlProvisioner(BgpProvisioner):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)
        if self._args.peer_list:
            peer_list = self._args.peer_list.split(',')
        else:
            peer_list = None
        super(ControlProvisioner, self).__init__(self._args.admin_user, self._args.admin_password,
                                                 self._args.admin_tenant_name,
                                                 self._args.api_server_ip, self._args.api_server_port,
                                                 api_server_use_ssl=self._args.api_server_use_ssl,
                                                 use_admin_api=self._args.use_admin_api,
                                                 sub_cluster_name=self._args.sub_cluster_name,
                                                 peer_list=peer_list)
    # end __init__

    def noop(self):
        return

    def run(self):
        if self.check_if_provision_is_needed():
            if self._args.router_asn and not self._args.oper:
                return self.global_bgp_provisioning
            elif self._args.oper in ['add', 'del']:
                return self.provision_control_bgp
            else:
                print("Unknown operation %s. Only 'add' and 'del' supported" \
                    % (self._args.oper))
        return self.noop

    def global_bgp_provisioning(self):
        print("Perfroming provisioning of global BGP parameters")
        gsc_obj = self._vnc_lib.global_system_config_read(
                fq_name=['default-global-system-config'])
        # global asn might have been modified in clusters.
        # so provision_control should not set back to 64512(default)
        if (self._args.router_asn != '64512'):
            gsc_obj.set_autonomous_system(self._args.router_asn)
        if self._args.ibgp_auto_mesh is not None:
            gsc_obj.set_ibgp_auto_mesh(self._args.ibgp_auto_mesh)

        if self._args.set_graceful_restart_parameters == True:
            gr_params = GracefulRestartParametersType()
            gr_params.set_restart_time(
                int(self._args.graceful_restart_time))
            gr_params.set_long_lived_restart_time(
                int(self._args.long_lived_graceful_restart_time))
            gr_params.set_end_of_rib_timeout(
                int(self._args.end_of_rib_timeout))
            gr_params.set_enable(self._args.graceful_restart_enable)
            gr_params.set_bgp_helper_enable(
                self._args.graceful_restart_bgp_helper_enable)
            gr_params.set_xmpp_helper_enable(
                self._args.graceful_restart_xmpp_helper_enable)
            gsc_obj.set_graceful_restart_parameters(gr_params)
        if self._args.enable_4byte_as is not None:
            gsc_obj.set_enable_4byte_as(self._args.enable_4byte_as)
        self._vnc_lib.global_system_config_update(gsc_obj)

    def check_if_provision_is_needed(self):
        if self._args.host_name is None:
            hostname = self._vnc_lib.hostname
        else:
            hostname = self._args.host_name
        if not self._args.oper or self._args.oper == 'add':
            try:
                router_obj = self._vnc_lib.bgp_router_read(
                    fq_name = self._get_rt_inst_obj().fq_name + [hostname],
                    fields=['global_system_config_back_refs'])
                if not router_obj.get_global_system_config_back_refs():
                    print("global-system-config-bgp-router link is not present")
                    return True
                print("Skip provisioning, router already exists")
                return False
            except NoIdError:
                return True
            except Exception as e:
                print("Problem with API query: %s" % str(e))
        elif self._args.oper == 'del':
            print("Deleting controller")
            return True
        return True

    def provision_control_bgp(self):
        if self._args.oper == 'add':
            if self._args.sub_cluster_name:
                print("Perfroming provisioning of controller specific BGP parameters")
                self.add_bgp_router('external-control-node',
                                self._args.host_name,
                                self._args.host_ip, self._args.router_asn,
                                self._args.address_families, self._args.md5,
                                self._args.local_autonomous_system,
                                self._args.bgp_server_port)
            else:
                self.add_bgp_router('control-node', self._args.host_name,
                                self._args.host_ip, self._args.router_asn,
                                self._args.address_families, self._args.md5,
                                self._args.local_autonomous_system,
                                self._args.bgp_server_port)
        elif self._args.oper == 'del':
            self.del_bgp_router(self._args.host_name)
        return


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
                                        --enable_4byte_as
                                        --ibgp_auto_mesh|--no_ibgp_auto_mesh
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --api_server_use_ssl False
                                        --oper <add | del>
                                        --md5 <key value>|None(optional)
                                        --bgp_server_port <port>|None(optional)
                                        --local_autonomous_system <ASN value>|None(optional)
                                        --graceful_restart_time 300
                                        --long_lived_graceful_restart_time 300
                                        --end_of_rib_timeout 300
                                        --set_graceful_restart_parameters False
                                        --graceful_restart_bgp_helper_enable False
                                        --graceful_restart_xmpp_helper_enable False
                                        --graceful_restart_enable False

        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'router_asn': '64512',
            'enable_4byte_as': None,
            'bgp_server_port': 179,
            'local_autonomous_system': None,
            'ibgp_auto_mesh': None,
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
            'api_server_use_ssl': False,
            'oper': None,
            'admin_user': None,
            'admin_password': None,
            'admin_tenant_name': None,
            'md5' : None,
            'graceful_restart_time': 300,
            'long_lived_graceful_restart_time': 300,
            'end_of_rib_timeout': 300,
            'graceful_restart_bgp_helper_enable': False,
            'graceful_restart_xmpp_helper_enable': False,
            'graceful_restart_enable': False,
            'set_graceful_restart_parameters': False,
            'sub_cluster_name': None,
            'peer_list':None,
        }

        if args.conf_file:
            config = configparser.SafeConfigParser()
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
            "--enable_4byte_as", help="If set, AS Number can be 4 byte wide", dest='enable_4byte_as', action='store_true')
        parser.add_argument(
            "--bgp_server_port", help="BGP server port number (Default: 179)")
        parser.add_argument(
            "--local_autonomous_system", help="Local autonomous-system number used to peer contrail-control bgp speakers across different geographic locations")
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
        parser.add_argument("--api_server_port", help="Port of api server")
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
            type=self.gr_time_type, default=300,
            required=False)
        parser.add_argument(
            "--long_lived_graceful_restart_time",
            help="Long Lived Graceful Restart Time in seconds (0..16777215)",
            type=self.llgr_time_type, default=300,
            required=False)
        parser.add_argument(
            "--end_of_rib_timeout",
            help="EndOfRib timeout value in seconds (0..4095)",
            type=self.gr_time_type, default=300,
            required=False)
        parser.add_argument("--graceful_restart_bgp_helper_enable",
                            action='store_true',
                            help="Enable helper mode for BGP graceful restart")
        parser.add_argument("--graceful_restart_xmpp_helper_enable",
                            action='store_true',
                            help="Enable helper mode for XMPP graceful restart")
        parser.add_argument("--graceful_restart_enable",
                            action='store_true',
                            help="Enable Graceful Restart")
        parser.add_argument("--set_graceful_restart_parameters",
                            action='store_true',
                            help="Set Graceful Restart Parameters")
        parser.add_argument(
            "--sub_cluster_name", help="sub cluster to associate to",
            required=False)
        parser.add_argument(
            "--peer_list", help="list of control node names to peer",
            required=False)
        group = parser.add_mutually_exclusive_group(required=True)
        group.add_argument(
            "--api_server_ip", help="IP address of api server",
            nargs='+', type=str)
        group.add_argument("--use_admin_api",
                            default=False,
                            help = "Connect to local api-server on admin port",
                            action="store_true")

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

# end class ControlProvisioner


def main(args_str=None):
    provisioner = ControlProvisioner(args_str).run()
    provisioner()
# end main

if __name__ == "__main__":
    main()
