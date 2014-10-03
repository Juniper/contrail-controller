#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import os
import sys
import errno
import subprocess
import time
import argparse

sys.path.insert(0, os.path.realpath('/usr/lib/python2.7/site-packages'))
sys.path.insert(
    0,
    os.path.realpath('/usr/lib/python2.7/site-packages/vnc_cfg_api_server/'))

from vnc_api.vnc_api import *
from vnc_api.common import exceptions as vnc_exceptions
import vnc_cfg_api_server
from svc_monitor import svc_monitor


class ServicePolicyCmd(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._domain_fq_name = [self._args.domain_name]
        self._proj_fq_name = [self._args.domain_name, self._args.proj_name]
        self._policy_fq_name = [self._args.domain_name, self._args.proj_name,
                                self._args.policy_name]

        self._vn_fq_list = [[self._args.domain_name, self._args.proj_name, vn]
                            for vn in self._args.vn_list or []]
        self._svc_list = [":".join(self._proj_fq_name) + ':' +
                          s for s in self._args.svc_list or []]

        self._vnc_lib = VncApi('u', 'p',
                               api_server_host=self._args.api_server_ip,
                               api_server_port=self._args.api_server_port)
    # end __init__

    def _parse_args(self, args_str):
        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        global_defaults = {
            'domain_name': 'default-domain',
            'proj_name': 'demo',
            'svc_list': None,
            'vn_list': None,
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
        }

	if not args.conf_file:
            args.conf_file = '/etc/contrail/svc-monitor.conf'

        config = ConfigParser.SafeConfigParser()
        ret = config.read([args.conf_file])
        if args.conf_file not in ret:
            print "Error: Unable to read the config file %s" % args.conf_file
            sys.exit(-1)

        global_defaults.update(dict(config.items("DEFAULTS")))

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

        parser.set_defaults(**global_defaults)
        subparsers = parser.add_subparsers()

        create_parser = subparsers.add_parser('add')
        create_parser.add_argument("policy_name", help="service policy name")
        create_parser.add_argument(
            "--svc_list", help="service instance name(s)",
            nargs='+', required=True)
        create_parser.add_argument(
            "--vn_list", help="ordered list of VNs", nargs=2, required=True)
        create_parser.add_argument(
            "--proj_name", help="name of project [default: demo]")
        create_parser.set_defaults(func=self.create_policy)

        delete_parser = subparsers.add_parser('del')
        delete_parser.add_argument("policy_name", help="service policy name")
        delete_parser.add_argument(
            "--proj_name", help="name of project [default: demo]")
        delete_parser.set_defaults(func=self.delete_policy)
        self._args = parser.parse_args(remaining_argv)
    # end _parse_args

    def create_policy(self):
        if self._vn_fq_list == [] or self._svc_list == []:
            print "Error: VN list or Service list is empty"
            return

        mirror = None
        policy_flag = 'transparent'
        for svc in self._svc_list:
            try:
                si_obj = self._vnc_lib.service_instance_read(fq_name_str=svc)
            except NoIdError:
                print "Error: Service instance %s not found" % (svc)
                return

            st_list = si_obj.get_service_template_refs()
            if st_list is not None:
                fq_name = st_list[0]['to']
                st_obj = self._vnc_lib.service_template_read(fq_name=fq_name)
                service_type =\
                    st_obj.get_service_template_properties().get_service_type()
                if service_type == 'analyzer':
                    mirror = svc

            si_prop = si_obj.get_service_instance_properties()
            if si_prop:
                if ((si_prop.get_left_virtual_network() ==
                     self._args.vn_list[0]) and
                    (si_prop.get_right_virtual_network() ==
                     self._args.vn_list[1])):
                    policy_flag = 'in-network'

        if mirror is not None and len(self._svc_list) != 1:
            print "Error: Multiple service instances not allowed for analyzer"
            return

        if policy_flag == 'in-network' and len(self._svc_list) != 1:
            print "Error: Multiple service instances cannot "\
                  "be chained for in-network mode"
            return

        print "Create and attach policy %s" % (self._args.policy_name)
        project = self._vnc_lib.project_read(fq_name=self._proj_fq_name)
        try:
            vn_obj_list = [self._vnc_lib.virtual_network_read(vn)
                           for vn in self._vn_fq_list]
        except NoIdError:
            print "Error: VN(s) %s not found" % (self._args.vn_list)
            return

        addr_list = [AddressType(virtual_network=vn.get_fq_name_str())
                     for vn in vn_obj_list]

        port = PortType(0, -1)
        action_list = None
        action = "pass"
        if self._svc_list:
            action_list = ActionListType(apply_service=self._svc_list,
                                         service_chain_type=policy_flag)
            action = None
            timer = None

        if mirror:
            mirror_action = MirrorActionType(mirror)
            action_list = ActionListType(mirror_to=mirror_action)
            action = None
            timer = TimerType()

        prule = PolicyRuleType(direction="<>", simple_action=action,
                               protocol="any", src_addresses=[addr_list[0]],
                               dst_addresses=[addr_list[1]], src_ports=[port],
                               dst_ports=[port], action_list=action_list)
        pentry = PolicyEntriesType([prule])
        np = NetworkPolicy(
            name=self._args.policy_name, network_policy_entries=pentry,
            parent_obj=project)
        self._vnc_lib.network_policy_create(np)

        seq = SequenceType(1, 1)
        vn_policy = VirtualNetworkPolicyType(seq, timer)
        for vn in vn_obj_list:
            vn.set_network_policy(np, vn_policy)
            self._vnc_lib.virtual_network_update(vn)

        return np
    # end create_policy

    def delete_policy(self):
        print "Deleting policy %s" % (self._args.policy_name)
        try:
            np = self._vnc_lib.network_policy_read(self._policy_fq_name)
        except NoIdError:
            print "Error: Policy %s not found for delete"\
                % (self._args.policy_name)
            return

        for network in (np.get_virtual_network_back_refs() or []):
            try:
                vn_obj = self._vnc_lib.virtual_network_read(
                    id=network['uuid'])
                for name in vn_obj.network_policy_refs:
                    if self._policy_fq_name == name['to']:
                        vn_obj.del_network_policy(np)
                        self._vnc_lib.virtual_network_update(vn_obj)
            except NoIdError:
                print "Error: VN %s not found for delete" % (network['to'])

        self._vnc_lib.network_policy_delete(fq_name=self._policy_fq_name)
    # delete_policy

# end class ServicePolicyCmd


def main(args_str=None):
    sp = ServicePolicyCmd(args_str)
    sp._args.func()
# end main

if __name__ == "__main__":
    main()
