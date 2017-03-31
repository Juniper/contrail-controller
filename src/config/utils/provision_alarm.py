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
from vnc_admin_api import VncApiAdmin
from cfgm_common.exceptions import *
from contrail_alarm import alarm_list


class AlarmProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        try:
            self._vnc_lib = VncApiAdmin(
                self._args.use_admin_api,
                self._args.admin_user,
                self._args.admin_password,
                self._args.admin_tenant_name,
                self._args.api_server_ip,
                self._args.api_server_port,
                api_server_use_ssl=self._args.api_server_use_ssl)
        except ResourceExhaustionError: # haproxy throws 503
            raise

        for alarm in alarm_list:
            kwargs = alarm
            fq_name = alarm['fq_name']
            alarm_obj = Alarm(**kwargs)

            try:
                self._vnc_lib.alarm_create(alarm_obj)
            except AttributeError:
                print "Invalid alarm config for %s" % (fq_name)
            except RefsExistError:
                print "alarm config %s already created" % (fq_name)
            except Exception as e:
                print "Failed to create alarm config %s - %s" % (fq_name, str(e))
            else:
                print "Created alarm %s" % (fq_name)
    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python provision_alarm.py --api_server_ip localhost
                                      --api_server_port 8082
                                      --admin_user admin
                                      --admin_password contrail123
                                      --admin_tenant_name admin
                                      --api_server_use_ssl False
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        parser = argparse.ArgumentParser(add_help=False)

        args, remaining_argv = parser.parse_known_args(args_str.split())

        parser.add_argument(
            "--api_server_port",
            default='8082',
            help="Port of api server")
        parser.add_argument(
            "--admin_user",
            help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password",
            help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name",
            help="Tenant name for keystone admin user")
        parser.add_argument(
            "--api_server_use_ssl",
            default=False,
            help="Use SSL to connect with API server"),
        group = parser.add_mutually_exclusive_group()
        group.add_argument(
            "--api_server_ip", help="IP address of api server")
        group.add_argument("--use_admin_api",
                            default=False,
                            help = "Connect to local api-server on admin port",
                            action="store_true")
        self._args = parser.parse_args(remaining_argv)
    # end _parse_args

# end class AlarmProvisioner

def main(args_str=None):
    AlarmProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
