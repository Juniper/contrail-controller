#!/usr/bin/python
# vim: tabstop=4 shiftwidth=4 softtabstop=4

#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Contrail CNI plugin for Kubernetes
"""


import inspect
import logging
import os
import sys
import traceback

# set parent directory in sys.path
cfile = os.path.abspath(inspect.getfile(inspect.currentframe()))  # nopep8
sys.path.append(os.path.dirname(os.path.dirname(cfile)))  # nopep8
from contrail.contrail_cni import ContrailCni as ContrailCni
from common.cni import Cni as Cni
from common.cni import Error as CniError
from contrail.contrail_cni import Error as ContrailError
from kube_params import Error as K8SParamsError
from kube_params import K8SParams as K8SParams


# Error codes
UNEXPECTED_PARAMS_ERROR = 1001


# logger for the file
logger = None


def main():
    try:
        cni = ContrailCni()

        # set logging
        global logger
        logger = logging.getLogger('contrail-kube-cni')

        k8s_params = K8SParams(cni.cni)
        cni.log()
        k8s_params.log()
        cni.Run()
    except CniError as err:
        err.log()
        Cni.error_exit(err.code, err.msg)
    except ContrailError as err:
        err.log()
        Cni.error_exit(err.code, err.msg)
    except K8SParamsError as err:
        err.log()
        Cni.error_exit(err.code, err.msg)
    except Exception as e:
        logger.exception("Unexpected error. " + str(e))
        err = traceback.format_exc(*e)
        Cni.error_exit(UNEXPECTED_PARAMS_ERROR,
                       "Unexpected error. " + str(e) +
                       " Exception "  + err)
    return

if __name__ == "__main__":
    main()
