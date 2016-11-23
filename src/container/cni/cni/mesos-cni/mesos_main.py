#!/usr/bin/python
# vim: tabstop=4 shiftwidth=4 softtabstop=4

#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
###  Contrail CNI plugin for MESOS ###
#

import sys
from common.conf import config as Config
from common.logger import logger as Logger
from common.netns import cni as Cni
from common.params import params as Params
from common.vrouter import vrouter as VRouter

# logger for the file
logger = None


def main():
    params = Params.Params()
    args = Config.parse_args()
    input_json = Config.read_config_file(args.file)

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
