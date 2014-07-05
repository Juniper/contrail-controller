#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import os
import sys
import errno
import pprint
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


class ServiceTemplateCmd(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        if self._args.svc_type in ['analyzer', 'source-nat']:
            self._if_list = [
                ['management', False], ['left', self._args.svc_scaling]]
        else:
            self._if_list = [['management', False], [
                'left', self._args.svc_scaling], ['right', False]]

        self._st_fq_name = [self._args.domain_name, self._args.template_name]
        self._domain_fq_name = [self._args.domain_name]

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
            'image_name': 'vsrx',
            'svc_scaling': False,
            'svc_type': 'firewall',
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
            "template_name", help="service template name")
        create_parser.add_argument(
            "--svc_type", help="firewall or analyzer [default: firewall]",
            choices=['firewall', 'analyzer', 'source-nat'])
        create_parser.add_argument(
            "--image_name", help="glance image name [default: vsrx]")
        create_parser.add_argument(
            "--flavor", help="Instance flavor")
        create_parser.add_argument(
            "--svc_scaling", action="store_true", default=False,
            help="enable service scaling [default: False]")
        create_parser.add_argument(
            "--svc_virt_type", default='virtual-machine',
            help="define virtualization type [default: virtual-machine]")

        create_parser.set_defaults(func=self.create_st)

        delete_parser = subparsers.add_parser('del')
        delete_parser.add_argument(
            "template_name", help="service template name")
        delete_parser.set_defaults(func=self.delete_st)

        list_parser = subparsers.add_parser('list')
        list_parser.set_defaults(func=self.list_st)

        self._args = parser.parse_args(remaining_argv)
    # end _parse_args

    # create service template
    def create_st(self):
        print "Creating service template %s" % (self._args.template_name)
        try:
            st_obj = self._vnc_lib.service_template_read(
                fq_name=self._st_fq_name)
            st_uuid = st_obj.uuid
        except NoIdError:
            domain = self._vnc_lib.domain_read(fq_name=self._domain_fq_name)
            st_obj = ServiceTemplate(
                name=self._args.template_name, domain_obj=domain)
            st_uuid = self._vnc_lib.service_template_create(st_obj)

        svc_properties = ServiceTemplateType()
        svc_properties.set_image_name(self._args.image_name)
        if self._args.flavor:
            svc_properties.set_flavor(self._args.flavor)
        svc_properties.set_service_scaling(True)
        svc_properties.set_service_type(self._args.svc_type)
        svc_properties.set_service_virtualization_type(self._args.svc_virt_type)

        # set interface list
        for itf in self._if_list:
            if_type = ServiceTemplateInterfaceType(shared_ip=itf[1])
            if_type.set_service_interface_type(itf[0])
            svc_properties.add_interface_type(if_type)

        st_obj.set_service_template_properties(svc_properties)
        self._vnc_lib.service_template_update(st_obj)

        return st_uuid
    # create_st

    def delete_st(self):
        try:
            print "Deleting service template %s" % (self._args.template_name)
            self._vnc_lib.service_template_delete(fq_name=self._st_fq_name)
        except NoIdError:
            print "Error: Service template %s not found"\
                % (self._args.template_name)
            return
    #_delete_st

    def list_st(self):
        print "Listing service templates"
        templates = self._vnc_lib.service_templates_list()
        pprint.pprint(templates)
    #_list_st

# end class ServiceTemplateCmd


def main(args_str=None):
    st = ServiceTemplateCmd(args_str)
    st._args.func()
# end main

if __name__ == "__main__":
    main()
