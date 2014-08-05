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
from novaclient import client as nc
from novaclient import exceptions as nc_exc


class ServiceInstanceCmd(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._proj_fq_name = [self._args.domain_name, self._args.proj_name]
        self._si_fq_name = [self._args.domain_name,
                            self._args.proj_name,
                            self._args.instance_name]
        self._st_fq_name = [self._args.domain_name, self._args.template_name]
        self._domain_fq_name = [self._args.domain_name]
        if self._args.left_vn:
            self._left_vn_fq_name = [self._args.domain_name,
                                     self._args.proj_name,
                                     self._args.left_vn]
        if self._args.right_vn:
            self._right_vn_fq_name = [self._args.domain_name,
                                      self._args.proj_name,
                                      self._args.right_vn]
        if self._args.mgmt_vn:
            self._mgmt_vn_fq_name = [self._args.domain_name,
                                     self._args.proj_name,
                                     self._args.mgmt_vn]

        self._novaclient_init()
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
            'template_name': None,
            'instance_name': None,
            'proj_name': 'demo',
            'mgmt_vn': None,
            'left_vn': None,
            'right_vn': None,
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
        create_parser.add_argument(
            "instance_name", help="service instance name")
        create_parser.add_argument(
            "template_name", help="service template name")
        create_parser.add_argument(
            "--proj_name", help="name of project [default: demo]")
        create_parser.add_argument(
            "--mgmt_vn", help="name of management vn [default: none]")
        create_parser.add_argument(
            "--left_vn", help="name of left vn [default: none]")
        create_parser.add_argument(
            "--right_vn", help="name of right vn [default: none]")
        create_parser.add_argument("--max_instances", type=int,  default=1,
                                   help="max instances to launch [default: 1]")
        create_parser.add_argument(
            "--auto_scale", action="store_true", default=False,
            help="enable auto-scale from 1 to max_instances")
        create_parser.set_defaults(func=self.create_si)

        delete_parser = subparsers.add_parser('del')
        delete_parser.add_argument(
            "instance_name", help="service instance name")
        delete_parser.add_argument(
            "template_name", help="service instance name")
        delete_parser.add_argument(
            "--proj_name", help="name of project [default: demo]")
        delete_parser.set_defaults(func=self.delete_si)

        self._args = parser.parse_args(remaining_argv)
    # end _parse_args

    def _novaclient_init(self):
        self._nova = nc.Client(
            '2', username='admin',
            project_id=self._args.proj_name, api_key='contrail123',
            auth_url='http://' + self._args.ifmap_server_ip + ':5000/v2.0')
    # end _novaclient_init

    # create service instance
    def create_si(self):
        # get service template
        try:
            st_obj = self._vnc_lib.service_template_read(
                fq_name=self._st_fq_name)
            st_prop = st_obj.get_service_template_properties()
            if st_prop is None:
                print "Error: Service template %s properties not found"\
                    % (self._args.template_name)
                return
        except NoIdError:
            print "Error: Service template %s not found"\
                % (self._args.template_name)
            return

        if st_prop.get_image_name():
            # check if image exists
            try:
                self._nova.images.find(name=st_prop.get_image_name())
            except nc_exc.NotFound:
                print "Error: Image %s not found" % (st_prop.get_image_name())
                return

        # check if passed VNs exist
        if self._args.left_vn:
            try:
                self._vnc_lib.virtual_network_read(
                    fq_name=self._left_vn_fq_name)
            except NoIdError:
                print "Error: Left VN %s not found" % (self._left_vn_fq_name)
                return
        if self._args.right_vn:
            try:
                self._vnc_lib.virtual_network_read(
                    fq_name=self._right_vn_fq_name)
            except NoIdError:
                print "Error: Right VN %s not found" % (self._right_vn_fq_name)
                return
        if self._args.mgmt_vn:
            try:
                self._vnc_lib.virtual_network_read(
                    fq_name=self._mgmt_vn_fq_name)
            except NoIdError:
                print "Error: Management VN %s not found" % (self._mgmt_vn_fq_name)
                return
        else:
            self._mgmt_vn_fq_name = []
            

        # create si
        print "Creating service instance %s" % (self._args.instance_name)
        project = self._vnc_lib.project_read(fq_name=self._proj_fq_name)
        try:
            si_obj = self._vnc_lib.service_instance_read(
                fq_name=self._si_fq_name)
            si_uuid = si_obj.uuid
        except NoIdError:
            si_obj = ServiceInstance(
                self._args.instance_name, parent_obj=project)
            si_uuid = self._vnc_lib.service_instance_create(si_obj)

        si_prop = ServiceInstanceType(
            left_virtual_network=':'.join(self._left_vn_fq_name),
            management_virtual_network=':'.join(self._mgmt_vn_fq_name),
            right_virtual_network=':'.join(self._right_vn_fq_name))

        # set scale out
        scale_out = ServiceScaleOutType(
            max_instances=self._args.max_instances,
            auto_scale=self._args.auto_scale)
        si_prop.set_scale_out(scale_out)

        si_obj.set_service_instance_properties(si_prop)
        st_obj = self._vnc_lib.service_template_read(id=st_obj.uuid)
        si_obj.set_service_template(st_obj)
        self._vnc_lib.service_instance_update(si_obj)

        return si_uuid
    # end create_si

    def delete_si(self):
        try:
            print "Deleting service instance %s" % (self._args.instance_name)
            self._vnc_lib.service_instance_delete(fq_name=self._si_fq_name)
        except NoIdError:
            return
    # delete_si

# end class ServiceInstanceCmd


def main(args_str=None):
    si = ServiceInstanceCmd(args_str)
# end main

if __name__ == "__main__":
    main()
