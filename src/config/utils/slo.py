#!/usr/bin/python
#
#Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from future import standard_library
standard_library.install_aliases()
from builtins import str
from builtins import object
import sys
import argparse
import configparser

from requests.exceptions import ConnectionError
from vnc_api.vnc_api import *

class CreateDeleteSLO(object):
    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        if not self._parse_args(args_str):
            return

        self._vnc_lib = VncApi(self._args.admin_user, self._args.admin_password,
                               self._args.admin_tenant_name,
                               self._args.api_server_ip, self._args.api_server_port, '/')

        if self._args.oper == 'associate' or self._args.oper == 'disassociate':
            self.HandleAssociateDisassociate()
            return

        slo_fq_name = "default-domain:default-project:" + self._args.name
        if self._args.parent == 'global-vrouter':
            slo_fq_name = "default-global-system-config:default-global-vrouter-config:" + self._args.name
        try:
            slo = self._vnc_lib.security_logging_object_read(fq_name = None, fq_name_str=slo_fq_name)
            if slo:
                if self._args.oper == 'add':
                    print("security-logging-object %s already exists" %(slo_fq_name))
                    print("    Details %s", str(slo))
                else:
                    self._vnc_lib.security_logging_object_delete(fq_name = slo.get_fq_name())
                    print("security-logging-object %s deleted successfully" %(slo_fq_name))
                    return
        except NoIdError:
            if self._args.oper == 'delete':
                print("security-logging-object %s does NOT exist" %(slo_fq_name))
                return
            parent = None

            if self._args.parent == 'global-vrouter':
                parent = self._vnc_lib.global_vrouter_config_read(fq_name=None, fq_name_str="default-global-system-config:default-global-vrouter-config")
            else:
                parent = self._vnc_lib.project_read(fq_name=None, fq_name_str="default-domain:default-project")
            slo = SecurityLoggingObject(name = self._args.name, parent_obj=parent, security_logging_object_rules=None, security_logging_object_rate=self._args.rate)
            self._vnc_lib.security_logging_object_create(slo)
    #end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python slo.py --name slo1 --rate 10 --oper=add --parent="default-project"
            python slo.py --name slo1 --rate 10 --oper=add --parent="global-vrouter"
            python slo.py --name slo1 --oper=delete --parent="default-project"
            python slo.py --slo_fq_name=default-domain:default-project:slo1 --oper=associate --vn_fq_name=default-domain:demo:vn1
            python slo.py --slo_fq_name=default-domain:default-project:slo1 --oper=disassociate --vn_fq_name=default-domain:demo:vn1
            python slo.py --slo_fq_name=default-domain:default-project:slo1 --oper=associate
                          --fw_policy_fq_name=default-policy-management:fp1
            python slo.py --slo_fq_name=default-domain:default-project:slo1 --oper=disassociate
                          --fw_policy_fq_name=default-policy-management:fp1
            python slo.py --slo_fq_name=default-domain:default-project:slo1 --oper=associate
                          --fw_rule_fq_name=default-policy-management:82266dd8-2eb3-470c-ba21-80be8c11fc83
            python slo.py --slo_fq_name=default-domain:default-project:slo1 --oper=disassociate
                          --fw_rule_fq_name=default-policy-management:82266dd8-2eb3-470c-ba21-80be8c11fc83
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help = False)

        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'api_server_ip' : '127.0.0.1',
            'api_server_port' : '8082',
            'admin_user': 'admin',
            'admin_password': 'contrail123',
            'admin_tenant_name': 'admin'
        }

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

        parser.add_argument("--name", help = "Name of Security logging object")
        parser.add_argument("--oper", choices=['add', 'delete', 'associate', 'disassociate'], help = "operation type - create/remove/associate/disassociate SLO", required=True)
        parser.add_argument("--parent", choices=['global-vrouter', 'default-project'], help = "Parent object type")
        parser.add_argument("--rate", type = int, default = 100, help = "Every 'rate' th flow matching SLO is logged")
        parser.add_argument("--slo_fq_name", help = "Fully qualified name of Security logging object")
        parser.add_argument("--vn_fq_name", help = "Fully qualified name of Virtual Network object")
        parser.add_argument("--vmi_fq_name", help = "Fully qualified name of Virtual Machine Interface object")
        parser.add_argument("--fw_rule_fq_name", help = "Fully qualified name of Firewall rule object")
        parser.add_argument("--fw_policy_fq_name", help = "Fully qualified name of Firewall policy object")
        parser.add_argument("--api_server_ip", help = "IP address of api server")
        parser.add_argument("--api_server_port", type = int, help = "Port of api server")
        parser.add_argument("--admin_user", help = "Name of keystone admin user")
        parser.add_argument("--admin_password", help = "Password of keystone admin user")
        parser.add_argument("--admin_tenant_name", help = "Tenamt name for keystone admin user")

        self._args = parser.parse_args(remaining_argv)

        ret_value  = True
        if self._args.oper == 'associate' or self._args.oper == 'disassociate':
            if not self._args.slo_fq_name:
                print("SLO FQ-name required for --oper=associate/disassociate")
                ret_value = False
            if not self._args.vn_fq_name and not self._args.vmi_fq_name and not self._args.fw_rule_fq_name and \
                not self._args.fw_policy_fq_name:
                print("VN/VMI/Firewall-rule/Firewall-policy FQ-name required for --oper=associate/disassociate")
                ret_value = False
        else:
            if not self._args.name:
                print("Argument --name required for --oper=add or --oper=delete")
                ret_value = False
            if not self._args.parent:
                print("Argument --parent required for --oper=add or --oper=delete")
                ret_value = False
        return ret_value
    #end _parse_args

    def HandleAssociateDisassociate(self):
        try:
            slo = self._vnc_lib.security_logging_object_read(fq_name = None, fq_name_str=self._args.slo_fq_name)
        except NoIdError:
            print("security-logging-object %s does NOT exist" %(self._args.slo_fq_name))
            return
        if self._args.vn_fq_name:
            try:
                vn = self._vnc_lib.virtual_network_read(fq_name_str = self._args.vn_fq_name)
            except NoIdError:
                print("Virtual Network %s does NOT exist" %(self._args.vn_fq_name))
                return
            if self._args.oper == 'associate':
                vn.add_security_logging_object(slo)
            elif self._args.oper == 'disassociate':
                vn.del_security_logging_object(slo)
            self._vnc_lib.virtual_network_update(vn)
        elif self._args.vmi_fq_name:
            try:
                vmi = self._vnc_lib.virtual_machine_interface_read(fq_name_str = self._args.vmi_fq_name)
            except NoIdError:
                print("Virtual Machine Interface %s does NOT exist" %(self._args.vmi_fq_name))
                return
            if self._args.oper == 'associate':
                vmi.add_security_logging_object(slo)
            elif self._args.oper == 'disassociate':
                vmi.del_security_logging_object(slo)
            self._vnc_lib.virtual_machine_interface_update(vmi)
        elif self._args.fw_policy_fq_name:
            try:
                fp = self._vnc_lib.firewall_policy_read(fq_name_str = self._args.fw_policy_fq_name)
            except NoIdError:
                print("Firewall Policy %s does NOT exist" %(self._args.fw_policy_fq_name))
                return
            if self._args.oper == 'associate':
                fp.add_security_logging_object(slo)
            elif self._args.oper == 'disassociate':
                fp.del_security_logging_object(slo)
            self._vnc_lib.firewall_policy_update(fp)
        elif self._args.fw_rule_fq_name:
            try:
                fr = self._vnc_lib.firewall_rule_read(fq_name_str = self._args.fw_rule_fq_name)
            except NoIdError:
                print("Firewall rule %s does NOT exist" %(self._args.fw_rule_fq_name))
                return
            if self._args.oper == 'associate':
                fr.add_security_logging_object(slo)
            elif self._args.oper == 'disassociate':
                fr.del_security_logging_object(slo)
            self._vnc_lib.firewall_rule_update(fr)
    #end HandleAssociateDisassociate

#end class CreateDeleteSLO

def main(args_str = None):
    CreateDeleteSLO(args_str)
#end main

if __name__ == "__main__":
    main()
