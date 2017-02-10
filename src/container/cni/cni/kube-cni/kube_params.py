
# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Kubernetes CNI plugin parameters processing module
"""

import inspect
import json
import logging
import os
import sys


# set parent directory in sys.path
cfile = os.path.abspath(inspect.getfile(inspect.currentframe()))  # nopep8
sys.path.append(os.path.dirname(os.path.dirname(cfile)))  # nopep8
from common.cni import Cni as Cni


# Error codes from kube_params module
K8S_PARAMS_ERR_DOCKER_CONNECTION = 1101
K8S_PARAMS_ERR_GET_UUID = 1102
K8S_ARGS_MISSING_POD_NAME = 1103

# Logger for the file
logger = None


class Error(RuntimeError):
    '''
    Exception class to report CNI processing errors
    '''

    def __init__(self, code, msg):
        self.msg = msg
        self.code = code
        return

    def log(self):
        logger.error(str(self.code) + ' : ' + self.msg)
        return


class K8SParams():
    '''
    Kubernetes specific parameters. Will contain parameters not generic to CNI
    pod_uuid - UUID for the POD. Got from "docker inspect" equivalent
    pod_name - Name of POD got from CNI_ARGS
    '''

    def __init__(self, cni):
        self.cni = cni
        self.pod_uuid = cni.container_uuid
        self.pod_name = None
        # set logging
        global logger
        logger = logging.getLogger('k8s-params')
        self._get_params()
        self._get_pod_info()
        self.cni.update(self.pod_uuid, self.pod_name)
        return

    def _get_pod_info(self):
        '''
        Get UUID for POD using "docker inspect" equivalent API
        '''
        from docker import client
        os.environ['DOCKER_API_VERSION'] = '1.22'
        try:
            docker_client = client.APIClient()
            if docker_client == None:
                raise Error(K8S_PARAMS_ERR_DOCKER_CONNECTION,
                            'Error creating docker client')
            container = docker_client.inspect_container(self.cni.container_id)
            self.pod_uuid = container['Config']['Labels']\
                ['io.kubernetes.pod.uid']
        except:
            # Dont report exception if pod_uuid set from argument already
            # pod-uuid will be specified in argument in case of UT
            if self.pod_uuid == None:
                raise Error(K8S_PARAMS_ERR_GET_UUID,
                            'Error finding UUID for pod ' +
                            self.cni.container_id)
        return

    def _get_params(self):

        '''
        In K8S, CNI_ARGS is of format
        "IgnoreUnknown=1;K8S_POD_NAMESPACE=default;\
        K8S_POD_NAME=hello-world-1-81nl8;\
        K8S_POD_INFRA_CONTAINER_ID=<container-id>"
        Get pod-name and infra-container-id from this
        '''
        args = Cni.get_env('CNI_ARGS')
        args_list = args.split(";")
        for x in args_list:
            vars_list = x.split('=')
            if vars_list == None:
                continue

            if len(vars_list) >= 2:
                if vars_list[0] == 'K8S_POD_NAME':
                    self.pod_name = vars_list[1]

        if self.pod_name == None:
            raise Error(K8S_ARGS_MISSING_POD_NAME,
                        'K8S_POD_NAME not set in CNI_ARGS <' + args + '>')
        return

    def log(self):
        logger.debug('K8SParams pod_uuid = ' + str(self.pod_uuid) +
                     ' pod_name = ' + str(self.pod_name))
        return
