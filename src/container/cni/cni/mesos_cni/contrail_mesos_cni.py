#!/usr/bin/python
# vim: tabstop=4 shiftwidth=4 softtabstop=4

#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Contrail CNI plugin for MESOS
"""

import inspect
import logging
import os
import requests
import sys
import traceback

# set parent directory in sys.path
current_file = os.path.abspath(inspect.getfile(inspect.currentframe()))  # nopep8
sys.path.append(os.path.dirname(os.path.dirname(current_file)))  # nopep8
from contrail.contrail_cni import ContrailCni as ContrailCni
from common.cni import Cni as Cni
from common.cni import Error as CniError
from contrail.contrail_cni import Error as ContrailError


# Mesos-manager IP/Port
MESOS_MGR_IP   = '127.0.0.1'
MESOS_MGR_PORT = 6999

# Error codes
UNEXPECTED_PARAMS_ERROR = 1001
CNI_ERR_POST_PARAMS     = 1002


# logger for the file
logger = None

def get_json_params_request(cni):
    ret_dict = {}
    ret_dict['cmd'] = cni.command
    ret_dict['cid'] = cni.container_id
    ret_dict.update(cni.stdin_json)
    return ret_dict

def send_params_to_mesos_mgr(c_cni):
    url = 'http://%s:%s' %(MESOS_MGR_IP, MESOS_MGR_PORT)
    if c_cni.cni.command.lower() == Cni.CNI_CMD_ADD:
        url += '/add_cni_info'
    elif c_cni.cni.command.lower() == Cni.CNI_CMD_DEL:
        url += '/del_cni_info'

    cni_req =  get_json_params_request(c_cni.cni)
    r = requests.post('%s' %(url), json=cni_req)

    if r.status_code != requests.status_codes.codes.ok:
        raise CniError(CNI_ERR_POST_PARAMS,
                       'Error in Post ' + url +
                       ' HTTP Response code %s' %(r.status_code) +
                       ' HTTP Response Data ' + r.text)
    return

def main():
    try:
        cni = ContrailCni()

        # set logging
        global logger
        logger = logging.getLogger('contrail-mesos-cni')

        # Mesos passes container_uuid in container_id
        cni.cni.update(cni.cni.container_id, None, None)
        cni.log()
        send_params_to_mesos_mgr(cni)
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
