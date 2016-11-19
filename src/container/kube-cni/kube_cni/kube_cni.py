#!/usr/bin/python
# vim: tabstop=4 shiftwidth=4 softtabstop=4

#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Contrail CNI plugin
Support Kubernetes for now
"""

import sys
import os
import argparse
import json
from common import logger as Logger
from params import params as Params
from cni import cni as Cni
from vrouter import vrouter as VRouter

# logger for the file
logger = None


def read_config_file(conf_file=None):
    '''
    Read config file from STDIN or optionally from a file
    '''
    if conf_file:
        with open(conf_file, 'r') as f:
            input_str = f.read()
    else:
        input_str = sys.stdin.read()
    obj = json.loads(input_str)
    return obj


def parse_args():
    '''
    Define command-line arguemnts.
    Note: CNI does not use any command-line arguments. Arguments are used only
    when run as standalone application (ex: for UT)
    Command-line argument overrides environment variables
    '''
    parser = argparse.ArgumentParser(description='CNI Arguments')
    parser.add_argument('-c', '--command',
                        help='CNI command add/del/version/get/poll')
    parser.add_argument('-v', '--version', action='version', version='0.1')
    parser.add_argument('-f', '--file', help='Contrail CNI config file')
    parser.add_argument('-u', '--uuid', help='Container UUID')
    args = parser.parse_args()

    # Override CNI_COMMAND environment
    if args.command != None:
        os.environ['CNI_COMMAND'] = args.command
    return args


def main():
    params = Params.Params()
    args = parse_args()
    input_json = read_config_file(args.file)

    # Read logging parameters first and then create logger
    params.get_loggin_params(input_json)
    global logger
    logger = Logger.Logger('opencontrail', params.contrail_params.log_file,
                           params.contrail_params.log_level)

    try:
        # Set UUID from argument. If valid-uuid is found, it will overwritten
        # later. Useful in case of UT where valid uuid for pod cannot be found
        if args.uuid != None:
            params.k8s_params.set_pod_uuid(args.uuid)
        # Update parameters from environement and input-string
        params.get_params(input_json)
    except Params.ParamsError as params_err:
        params_err.log()
        Cni.ErrorExit(params_err.code, params_err.msg)

    # Log params for debugging
    params.log()
    try:
        contrail_params = params.contrail_params
        vrouter = VRouter.VRouter(contrail_params.vrouter_ip,
                                  contrail_params.vrouter_port,
                                  contrail_params.poll_timeout,
                                  contrail_params.poll_retries,
                                  contrail_params.directory,
                                  contrail_params.log_file,
                                  contrail_params.log_level)
        cni = Cni.Cni(vrouter, params)
        # Run CNI
        cni.Run(vrouter, params)
    except Cni.CniError as cni_err:
        cni_err.log()
        Cni.ErrorExit(cni_err.code, cni_err.msg)
    except VRouter.VRouterError as vr_err:
        vr_err.log()
        Cni.ErrorExit(vr_err.code, vr_err.msg)
    return

if __name__ == "__main__":
    main()
