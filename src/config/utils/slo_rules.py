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
import json

from requests.exceptions import ConnectionError
from vnc_api.vnc_api import *

class RulesToFromSLO(object):
    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        if not self._parse_args(args_str):
            return

        self._vnc_lib = VncApi(self._args.admin_user, self._args.admin_password,
                               self._args.admin_tenant_name,
                               self._args.api_server_ip, self._args.api_server_port, '/')

        #Read SLO
        try:
            slo = self._vnc_lib.security_logging_object_read(fq_name = None, fq_name_str=self._args.slo_fq_name)
        except NoIdError:
            print("security-logging-object %s does NOT exist" %(self._args.slo_fq_name))
            return

        #Parse json to build a dictionary of rule and rate
        rule_dict = {}
        if self._args.rules:
            #Create empty rule list
            rule_list = SecurityLoggingObjectRuleListType()
            rule_dict = json.loads(self._args.rules)
            #Add rules to the rule_list from rule_dict
            for key in rule_dict:
                print(key, " = ", rule_dict[key])
                entry = SecurityLoggingObjectRuleEntryType(rule_uuid=key, rate=rule_dict[key])
                rule_list.add_rule(entry)
            #Update SLO with built rule_list
            slo.set_security_logging_object_rules(rule_list)
            self._vnc_lib.security_logging_object_update(slo)
            return

        if self._args.sg_fq_name:
            if self._args.oper == "associate":
                self.AssociateSgRules(slo)
            else:
                self.DisAssociateSgRules(slo)
        elif self._args.policy_fq_name:
            if self._args.oper == "associate":
                self.AssociatePolicyRules(slo)
            else:
                self.DisAssociatePolicyRules(slo)

    #end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python slo_rules.py --slo_fq_name=default-domain:default-project:slo1 --rules={\"ff4da0ac-4225-4cdf-a652-65924e1116c3\":10\,\"5600fcb7-6e12-47da-b658-1f6d87710e7b\":20\,\"dccae58e-1ab1-44ca-b12d-e4180a6b7c4f\":30}
            python slo_rules.py --slo_fq_name=default-domain:default-project:slo1 --sg_fq_name=default-domain:demo:default --rate=20
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

        parser.add_argument("--oper", choices=['associate', 'disassociate'], help = "operation type - associate/disassociate network-policy/SG with SLO")
        parser.add_argument("--slo_fq_name", help = "Fully qualified name of Security logging object", required=True)
        parser.add_argument("--sg_fq_name", help = "Fully qualified name of Security Group object")
        parser.add_argument("--policy_fq_name", help = "Fully qualified name of Network policy object")
        parser.add_argument("--rules", help = "json as {\"rule-uuid2\": rate1, \"rule-uuid2\": rate2}")
        parser.add_argument("--rate", type = int, help = "Every 'rate' th flow matching SLO is logged")
        parser.add_argument("--api_server_ip", help = "IP address of api server")
        parser.add_argument("--api_server_port", type = int, help = "Port of api server")
        parser.add_argument("--admin_user", help = "Name of keystone admin user")
        parser.add_argument("--admin_password", help = "Password of keystone admin user")
        parser.add_argument("--admin_tenant_name", help = "Tenamt name for keystone admin user")

        self._args = parser.parse_args(remaining_argv)

        ret_value  = True
        if self._args.sg_fq_name and self._args.policy_fq_name:
            print("The arguments --sg_fq_name and --policy_fq_name cannot be specified together")
            ret_value = False

        if self._args.sg_fq_name and not self._args.oper:
            print("The argument --sg_fq_name requires --oper to be specified")
            ret_value = False

        if self._args.policy_fq_name and not self._args.oper:
            print("The argument --policy_fq_name requires --oper to be specified")
            ret_value = False

        if self._args.rules and self._args.sg_fq_name:
            print("The arguments --rules and --sg_fq_name cannot be specified together")
            ret_value = False

        if self._args.rules and self._args.policy_fq_name:
            print("The arguments --rules and --policy_fq_name cannot be specified together")
            ret_value = False

        if self._args.rules and self._args.rate:
            print("The arguments --rules and --rate cannot be specified together")
            ret_value = False
        return ret_value
    #end _parse_args

    def UpdateSLORules(self, slo, rule_list_obj):
        if rule_list_obj:
            #create empty SLO rule list
            slo_rule_list = SecurityLoggingObjectRuleListType()
            #Iterate SG rules, create SLO rule and add to SLO rule list
            rule_list = rule_list_obj.get_policy_rule()
            for rule in rule_list:
                print("rule-uuid ", str(rule.get_rule_uuid()))
                #Create SLO rule
                entry = SecurityLoggingObjectRuleEntryType(rule_uuid=rule.get_rule_uuid(), rate=self._args.rate)
                #Add to SLO rule list
                slo_rule_list.add_rule(entry)
            #Update SLO with built rule_list
            slo.set_security_logging_object_rules(slo_rule_list)
            self._vnc_lib.security_logging_object_update(slo)
    #end UpdateSLORules

    def AssociateSgRules(self, slo):
        try:
            sg = self._vnc_lib.security_group_read(fq_name = None, fq_name_str=self._args.sg_fq_name)
        except NoIdError:
            print("security-group %s does NOT exist" %(self._args.sg_fq_name))
            return
        #create empty SLO rule list
        slo_rule_list = SecurityLoggingObjectRuleListType()
        #Add SG to SLO. This will add all the rules from ACLs generated for SG to SLO
        slo.add_security_group(sg, slo_rule_list)
        self._vnc_lib.security_logging_object_update(slo)
    #end AssociateSgRules

    def DisAssociateSgRules(self, slo):
        try:
            sg = self._vnc_lib.security_group_read(fq_name = None, fq_name_str=self._args.sg_fq_name)
        except NoIdError:
            print("security-group %s does NOT exist" %(self._args.sg_fq_name))
            return
        #Remove SG from SLO.
        slo.del_security_group(sg)
        self._vnc_lib.security_logging_object_update(slo)
    #end DisAssociateSgRules

    def AssociatePolicyRules(self, slo):
        try:
            pol = self._vnc_lib.network_policy_read(fq_name = None, fq_name_str=self._args.policy_fq_name)
        except NoIdError:
            print("Network policy %s does NOT exist" %(self._args.policy_fq_name))
            return
        #create empty SLO rule list
        slo_rule_list = SecurityLoggingObjectRuleListType()
        #Add NetworkPolicy to SLO. This will add all the rules from ACLs generated for NetworkPolicy to SLO
        slo.add_network_policy(pol, slo_rule_list)
        self._vnc_lib.security_logging_object_update(slo)
    #end AssociatePolicyRules

    def DisAssociatePolicyRules(self, slo):
        try:
            pol = self._vnc_lib.network_policy_read(fq_name = None, fq_name_str=self._args.policy_fq_name)
        except NoIdError:
            print("Network policy %s does NOT exist" %(self._args.policy_fq_name))
            return
        #Remove NetworkPolicy from SLO.
        slo.del_network_policy(pol)
        self._vnc_lib.security_logging_object_update(slo)
    #end DisAssociatePolicyRules
#end class RulesToFromSLO

def main(args_str = None):
    RulesToFromSLO(args_str)
#end main

if __name__ == "__main__":
    main()
