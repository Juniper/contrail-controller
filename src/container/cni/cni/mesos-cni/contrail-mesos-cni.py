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
current_file = os.path.abspath(inspect.getfile(inspect.currentframe()))  # nopep8
sys.path.append(os.path.dirname(os.path.dirname(current_file)))  # nopep8
from contrail.contrail_cni import ContrailCni as ContrailCni
from common.cni import Cni as Cni
from common.cni import Error as CniError
from contrail.contrail_cni import Error as ContrailError


# Error codes
UNEXPECTED_PARAMS_ERROR = 1001


# logger for the file
logger = None


def main():
    try:
        cni = ContrailCni()

        # set logging
        global logger
        logger = logging.getLogger('contrail-mesos-cni')

        # Mesos passes container_uuid in container_id
        cni.cni.update(cni.cni.container_id, None, None)
        cni.log()
        cni.Run()
    except CniError as err:
        err.log()
        Cni.error_exit(err.code, err.msg)
    except ContrailError as err:
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
