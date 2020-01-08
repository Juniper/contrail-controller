#!/usr/bin/python
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
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

# Provision a PNF service chain
# Populate provision_pnf_servicechain.ini appropriately

class PnfScProvisioner(object):

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
                    auth_host=self._args.api_server_ip)
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
            self.add_pnf()
        elif self._args.oper == 'del':
            self.del_pnf()
        else:
            print("Unknown operation %s. Only 'add' and 'del' supported"\
                % (self._args.oper))

    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python service_appliance_set.py -c <conf_file>
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
            'domain_name': 'default-domain',
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
            "--oper", required=True,
            help="Provision operation to be done(add or del)")
        self._args = parser.parse_args(remaining_argv)
    # end _parse_args

    def add_service_appliance_set(self):
        gsc_obj = self._global_system_config_obj

        default_gsc_name = "default-global-system-config"
        default_gsc_fq_name = [default_gsc_name]
        sa_set_fq_name = [default_gsc_name, self._args.name]

        try:
            sas_obj = self._vnc_lib.service_appliance_set_read(
                    fq_name=sa_set_fq_name)
            print("Service Appliance Set Exists " + (sas_obj.uuid))
            return
        except NoIdError:
            sas_obj = ServiceApplianceSet(self._args.name, gsc_obj)

        if self._args.virtualization_type is not None:
            sas_obj.set_service_appliance_set_virtualization_type(
                    self._args.virtualization_type)
        kvp_array = []
        try:
            for r,c in self._args.properties.items():
                kvp = KeyValuePair(r,c)
                kvp_array.append(kvp)
            kvps = KeyValuePairs()
            if kvp_array:
                kvps.set_key_value_pair(kvp_array)
            sas_obj.set_service_appliance_set_properties(kvps)
        except AttributeError:
            pass
        sa_set_uuid = self._vnc_lib.service_appliance_set_create(sas_obj)
        print("Service Appliance Set Created " + (sa_set_uuid))
    # end add_service_appliance_set

    def del_service_appliance_set(self):
        gsc_obj = self._global_system_config_obj

        default_gsc_name = "default-global-system-config"
        default_gsc_fq_name = [default_gsc_name]
        sa_set_fq_name = [default_gsc_name, self._args.name]

        try:
            self._vnc_lib.service_appliance_set_delete(fq_name=sa_set_fq_name)
        except NoIdError:
            print("Error: Service Appliance Set does not exist %s"\
                                                         % (self._args.name))
        else:
            print("Deleted Service Appliance Set %s " % (self._args.name))
    # end del_service_appliance_set

    def add_service_template(self):
        template_name = self._args.name + "-ST"
        st_fq_name = [self._args.domain_name, template_name]
        domain_fq_name = [self._args.domain_name]
        try:
            st_obj = self._vnc_lib.service_template_read(
                fq_name=st_fq_name)
            st_uuid = st_obj.uuid
        except NoIdError:
            domain = self._vnc_lib.domain_read(fq_name=domain_fq_name)
            st_obj = ServiceTemplate(
                name=template_name, domain_obj=domain)
            st_uuid = self._vnc_lib.service_template_create(st_obj)
            print("Service Template Created " + (st_uuid))
        else:
            print("Service Template Exists " + (st_uuid))

        try:
            sas_obj = self._vnc_lib.service_appliance_set_read(
                    fq_name = None,
                    fq_name_str="default-global-system-config:" +
                    self._args.name)
            st_obj.set_service_appliance_set(sas_obj)
        except NoIdError:
                print("Cannot find %s" % (self._args.service_appliance_set_name))

        try:
            svc_properties = ServiceTemplateType()
            svc_properties.set_service_virtualization_type(
                                              self._args.virtualization_type)
            if_type = ServiceTemplateInterfaceType()
            if_type.set_service_interface_type('left')
            svc_properties.add_interface_type(if_type)
            if_type = ServiceTemplateInterfaceType()
            if_type.set_service_interface_type('right')
            svc_properties.add_interface_type(if_type)
        except AttributeError:
            print("Warning: Service template could not be fully updated "\
                                                            + (st_uuid))
        else:
            st_obj.set_service_template_properties(svc_properties)
            self._vnc_lib.service_template_update(st_obj)
            print("Service Template Updated " + (st_uuid))

    # end add_service_template

    def del_service_template(self):
        template_name = self._args.name + "-ST"
        st_fq_name = [self._args.domain_name, template_name]
        try:
            self._vnc_lib.service_template_delete(fq_name=st_fq_name)
        except NoIdError:
            print("Error: Service template does not exist %s "\
                                              % (template_name))
        else:
            print("Deleted Service Template " + (template_name))
    # end del_service_template

    def add_service_appliance(self):
        default_gsc_name = "default-global-system-config"
        appliance_name = self._args.name + "-SA"
        sas_fq_name = [default_gsc_name, self._args.name]
        sa_fq_name = [default_gsc_name, self._args.name, appliance_name]
        try:
            sas_obj = self._vnc_lib.service_appliance_set_read(
                                                       fq_name=sas_fq_name)
        except NoIdError:
            print("Error: Service Appliance Set does not exist %s "\
                                                       % (self._args.name))
            sys.exit(-1)

        sa_obj = ServiceAppliance(appliance_name, sas_obj)
        try:
            sa_obj = self._vnc_lib.service_appliance_read(fq_name=sa_fq_name)
            print("Service Appliance Exists " + (sa_obj.uuid))
        except NoIdError:
            # sa_uuid = self._vnc_lib.service_appliance_create(sa_obj)
            # print "Service Appliance Created " + (sa_uuid)
            pass

        try:
            kvp_array = []
            kvp = KeyValuePair("left-attachment-point",
                    default_gsc_name + ':' + self._args.left_attachment_point)
            kvp_array.append(kvp)
            kvp = KeyValuePair("right-attachment-point",
                   default_gsc_name + ':' + self._args.right_attachment_point)
            kvp_array.append(kvp)
            kvps = KeyValuePairs()
            kvps.set_key_value_pair(kvp_array)
            sa_obj.set_service_appliance_properties(kvps)
            sa_obj.set_service_appliance_virtualization_type(
                                             self._args.virtualization_type)
        except AttributeError:
            print("Warning: Some attributes of Service Appliance missing "\
                                                            + (appliance_name))

        try:
            pnf_left_intf_obj = self._vnc_lib.physical_interface_read(
                    fq_name=[default_gsc_name,
                             self._args.pnf_left_intf.split(":")[0],
                            self._args.pnf_left_intf.split(":")[-1]])
            attr = ServiceApplianceInterfaceType( interface_type='left')
            sa_obj.add_physical_interface(pnf_left_intf_obj, attr)
        except NoIdError:
            print("Error: Left PNF interface does not exist %s "\
                                                  % (self._args.pnf_left_intf))
            sys.exit(-1)
        except AttributeError:
            print("Error: Left PNF interface missing")
            sys.exit(-1)

        try:
            pnf_right_intf_obj = self._vnc_lib.physical_interface_read(
                    fq_name=[default_gsc_name,
                             self._args.pnf_right_intf.split(":")[0],
                            self._args.pnf_right_intf.split(":")[-1]])
            attr = ServiceApplianceInterfaceType( interface_type='right')
            sa_obj.add_physical_interface(pnf_right_intf_obj, attr)
        except NoIdError:
            print("Error: Right PNF interface does not exist %s "\
                                               % (self._args.pnf_right_intf))
            sys.exit(-1)
        except AttributeError:
            print("Error: Right PNF interface missing")
            sys.exit(-1)

        self._vnc_lib.service_appliance_create(sa_obj)
        print("Service Appliance Updated " + (sa_obj.uuid))
    # end add_service_appliance

    def del_service_appliance(self):
        default_gsc_name = "default-global-system-config"
        appliance_name = self._args.name + "-SA"
        sa_fq_name = [default_gsc_name, self._args.name, appliance_name]
        try:
            self._vnc_lib.service_appliance_delete(fq_name=sa_fq_name)
        except NoIdError:
            print("Error: Service Appliance does not exist " + (appliance_name))
        else:
            print("Deleted Service Appliance " + (appliance_name))
    # end del_service_instance

    def add_service_instance(self):
        si_name = self._args.name + "-SI"
        st_name = self._args.name + "-ST"
        si_fq_name = ['default-domain','admin', si_name]
        st_fq_name = ['default-domain', st_name]
        si_obj = ServiceInstance(fq_name=si_fq_name)
        si_obj.fq_name = si_fq_name
        try:
            si_obj = self._vnc_lib.service_instance_read(fq_name=si_fq_name)
            print("Service Instance exists " + (si_obj.uuid))
        except NoIdError:
            # si_uuid = self._vnc_lib.service_instance_create(si_obj)
            # print "Service Instance created " + (si_uuid)
            pass
        try:
            st_obj = self._vnc_lib.service_template_read(fq_name=st_fq_name)
            si_obj.add_service_template(st_obj)
        except NoIdError:
            print("Error! Service template not found " + (st_name))
            sys.exit(-1)

        try:
            kvp_array = []
            kvp = KeyValuePair("left-svc-vlan", self._args.left_svc_vlan)
            kvp_array.append(kvp)
            kvp = KeyValuePair("right-svc-vlan", self._args.right_svc_vlan)
            kvp_array.append(kvp)
            kvp = KeyValuePair("left-svc-asns", self._args.left_svc_asns)
            kvp_array.append(kvp)
            kvp = KeyValuePair("right-svc-asns", self._args.right_svc_asns)
            kvp_array.append(kvp)
            kvps = KeyValuePairs()
            kvps.set_key_value_pair(kvp_array)
            si_obj.set_annotations(kvps)
            props = ServiceInstanceType()
            props.set_service_virtualization_type(
                                               self._args.virtualization_type)
            props.set_ha_mode("active-standby")
            si_obj.set_service_instance_properties(props)
        except AttributeError:
            print("Warning: Some attributes of Service Instance missing "\
                                                                   + (si_name))
        self._vnc_lib.service_instance_create(si_obj)
        print("Service Instance created " + (si_obj.uuid))
    # end add_service_instance

    def del_service_instance(self):
        si_name = self._args.name + "-SI"
        si_fq_name = ['default-domain', 'admin', si_name]
        try:
            self._vnc_lib.service_instance_delete(fq_name=si_fq_name)
        except NoIdError:
            print("Error: Service Instance does not exist " + (si_name))
        else:
            print("Deleted Service Instance " + (si_name))
    # end del_service_instance

    def add_port_tuple(self):
        pt_name = self._args.name + "-PT"
        si_name = self._args.name + "-SI"
        si_fq_name = ['default-domain', 'admin', si_name]
        pt_fq_name = ['default-domain', 'admin', si_name, pt_name]
        try:
            si_obj = self._vnc_lib.service_instance_read(fq_name=si_fq_name)
        except NoIdError:
            print("Service Instance Not found " + (si_name))
            sys.exit(-1)
        pt_obj = PortTuple(pt_name, parent_obj=si_obj)
        try:
            pt_obj = self._vnc_lib.port_tuple_read(fq_name=pt_fq_name)
            print("Port Tuple Exists " + (pt_obj.uuid))
        except NoIdError:
            # pt_uuid = self._vnc_lib.port_tuple_create(pt_obj)
            # print "Port Tuple Created " + (pt_uuid)
            pass
        try:
            left_lr_fq_name = ['default-domain',
                               'admin',
                               self._args.left_lr_name]
            right_lr_fq_name = ['default-domain',
                                'admin',
                                self._args.right_lr_name]
            left_lr_obj = self._vnc_lib.logical_router_read(
                                                     fq_name=left_lr_fq_name)
            right_lr_obj = self._vnc_lib.logical_router_read(
                                                    fq_name=right_lr_fq_name)
            pt_obj.add_logical_router(left_lr_obj)
            pt_obj.add_logical_router(right_lr_obj)
        except NoIdError as e:
            print("Error! LR not found " + (e.message))
            sys.exit(-1)
    # Add annotations of left-LR UUID and right-LR UUID
    try:
        kvp_array = []
        kvp = KeyValuePair("left-lr", left_lr_obj.uuid)
        kvp_array.append(kvp)
        kvp = KeyValuePair("right-lr",right_lr_obj.uuid)
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        pt_obj.set_annotations(kvps)
    except AttributeError:
        print("Warning: Some attributes of PT missing " + pt_name)
        self._vnc_lib.port_tuple_create(pt_obj)
        print("Port Tuple Updated " + (pt_obj.uuid))
    # end add_port_tuple

    def del_port_tuple(self):
        pt_name = self._args.name + "-PT"
        si_name = self._args.name + "-SI"
        pt_fq_name = ['default-domain', 'admin', si_name, pt_name]
        try:
            self._vnc_lib.port_tuple_delete(fq_name=pt_fq_name)
        except NoIdError:
            print("Error: Port Tuple does not exist " + (pt_name))
        else:
            print("Deleted Port Tuple " + (pt_name))
        # end del_port_tuple

    def add_pnf(self):
        self.add_service_appliance_set()
        self.add_service_template()
        self.add_service_appliance()
        self.add_service_instance()
        self.add_port_tuple()
    # end add_pnf

    def del_pnf(self):
        self.del_port_tuple()
        self.del_service_instance()
        self.del_service_appliance()
        self.del_service_template()
        self.del_service_appliance_set()
    # end del_pnf

# end class PnfScProvisioner

def main(args_str=None):
    PnfScProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
