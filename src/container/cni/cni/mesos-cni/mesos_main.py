#!/usr/bin/python
# vim: tabstop=4 shiftwidth=4 softtabstop=4

#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
###  Contrail CNI plugin for MESOS ###
#

import sys
from cni.common import config as Config
from cni.common import logger as Logger
from cni.common import cni as Cni
from cni.common import vrouter as VRouter
from cni.common import params as Params
from mesos_params import MESOSParams as MParams

# logger for the file
logger = None


def read_stdin():
    return [ line for line in sys.stdin ]

def main():
    params = MParams()
    args = Config.parse_args()
    if args:
        conf_json = Config.read_config_file(args.file)
    else:
        conf_json = Config.read_config_file(params.log_file)
    stdin_json = read_stdin()

    # Read logging parameters first and then create logger
    params.get_loggin_params(conf_json)
    global logger
    logger = Logger.Logger('opencontrail', params.contrail_params.log_file,
                           params.contrail_params.log_level)

    try:
        # Update parameters from environment and input-string
        params.get_params(conf_json)
        # Update MESOS params from stdin
        params.get_stdin_params(stdin_json)
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
