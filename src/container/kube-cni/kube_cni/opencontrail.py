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
sys.path.insert(0, '/root/kube_cni')
sys.path.insert(0, '/usr/lib/python2.7/dist-packages')

import os
import argparse
import json
import common.logger as Logger
import params.params as Params
import cni.cni as Cni
import vrouter.vrouter as VRouter

# Read config file from STDIN or optionally from a file
def read_config_file(conf_file = None):
    if conf_file:
        with open(conf_file, 'r') as f:
            input_str = f.read()
    else:
        input_str = sys.stdin.read();
    obj = json.loads(input_str)
    return obj

# Define arguemnts.
# Note: CNI does not use any command-line arguments. These are used only when
# run as standalone application
# Command-line argument overrides environment variables
def parse_args():
    parser = argparse.ArgumentParser(description='CNI Arguments')
    parser.add_argument('-c', '--command',
                        help = 'CNI command add/del/version/get/poll')
    parser.add_argument('-v', '--version', action = 'version', version='0.1')
    parser.add_argument('-f', '--file', help = 'Contrail CNI config file')
    args = parser.parse_args()

    # Override CNI_COMMAND environment
    if args.command != None:
        os.environ['CNI_COMMAND'] = args.command
    return args

def main():
    args = parse_args()
    input_json = read_config_file(args.file)

    logger = Logger.Logger()
    cni = Cni.Cni(logger)
    try:
        params = Params.Params(logger)
        params.Get(input_json)
    except Params.ParamsError as params_err:
        params_err.Log(logger)
        cni.ErrorExit(params_err.code, params_err.msg)

    params.Log()

    try:
        contrail_params = params.contrail_params
        vrouter = VRouter.VRouter(logger, contrail_params.vrouter_ip,
                contrail_params.vrouter_port, contrail_params.poll_timeout,
                contrail_params.poll_retries, contrail_params.dir)
        cni.Run(vrouter, params)
    except Cni.CniError as cni_err:
        cni_err.Log(logger)
        cni.ErrorExit(cni_err.code, cni_err.msg)
    except VRouter.VRouterError as vr_err:
        vr_err.Log(logger)
        cni.ErrorExit(vr_err.code, vr_err.msg)
    return

if __name__ == "__main__":
    main()
