#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import json
import sys
import time
import argparse
import ConfigParser

from vnc_api.vnc_api import *
from cfgm_common.exceptions import *

class AlarmProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        try:
            self._vnc_lib = VncApi(
                self._args.admin_user,
                self._args.admin_password,
                self._args.admin_tenant_name,
                self._args.api_server_ip,
                self._args.api_server_port,
                api_server_use_ssl=self._args.api_server_use_ssl)
        except ResourceExhaustionError: # haproxy throws 503
            raise

        try:
            alarm_fp = open(self._args.alarm_file)
            alarm_str = alarm_fp.read()
            self._args.alarm_list = json.loads(alarm_str)
        except ValueError:
            print "Decoding json alarm-file failed."
        else:
            for alarm_name, value in self._args.alarm_list.iteritems():
                alarm_or_list = []
                for and_list in value['or_list']:
                    alarm_and_list = []
                    for input_exp in and_list['and_list']:
                        operand1=input_exp['operand1']
                        operation=input_exp['operation']
                        uve_attribute = input_exp['operand2'].get('uve_attribute')
                        json_value = input_exp['operand2'].get('json_value')
                        operand2=AlarmOperand2(json_value=json_value,
                                               uve_attribute=uve_attribute)
                        variables=input_exp.get('variables')
                        exp = AlarmExpression(
                                    operation=operation,
                                    operand1=operand1,
                                    operand2=operand2,
                                    variables=variables)
                        alarm_and_list.append(exp)
                    alarm_or_list.append(AlarmAndList(alarm_and_list))

                id_perms = IdPermsType(creator=value['creator'],
                                       description=value['description'])

                fq_name = ['default-global-system-config', alarm_name]
                kwargs = {'parent_type': 'global-system-config',
                          'fq_name': fq_name}

                alarm_obj = Alarm(name=alarm_name,
                    uve_keys=UveKeysType(value['uve_keys']),
                    alarm_severity=value['alarm_severity'],
                    alarm_rules=AlarmOrList(alarm_or_list),
                    id_perms=id_perms, **kwargs)

                if self._args.oper == 'add':
                    self._vnc_lib.alarm_create(alarm_obj)
                else:
                    self._vnc_lib.alarm_delete(fq_name=fq_name)
    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python provision_alarm.py --api_server_ip 127.0.0.1
                                      --api_server_port 8082
                                      --admin_user admin
                                      --admin_password contrail123
                                      --admin_tenant_name admin
                                      --api_server_use_ssl False
                                      --oper <add | del>
                                      --alarm_file /opt/contrail/utils/contrail-alarm.json
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        parser = argparse.ArgumentParser(add_help=False)

        args, remaining_argv = parser.parse_known_args(args_str.split())

        parser.add_argument(
            "--api_server_ip",
            default='127.0.0.1',
            help="IP address of api server")
        parser.add_argument(
            "--api_server_port",
            default='8082',
            help="Port of api server")
        parser.add_argument(
            "--admin_user",
            help="Name of keystone admin user",
            required=True)
        parser.add_argument(
            "--admin_password",
            help="Password of keystone admin user",
            required=True)
        parser.add_argument(
            "--admin_tenant_name",
            help="Tenant name for keystone admin user",
            required=True)
        parser.add_argument(
            "--api_server_use_ssl",
            default=False,
            help="Use SSL to connect with API server"),
        parser.add_argument(
            "--oper",
            default='add',
            help="Provision operation to be done(add or del)",
            choices=["add", "del"])
        parser.add_argument(
            "--alarm_file",
            help="Alarm list in json format",
            required=True)
        self._args = parser.parse_args(remaining_argv)
    # end _parse_args

# end class AlarmProvisioner

def main(args_str=None):
    AlarmProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
