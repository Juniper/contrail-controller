
# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Kubernetes CNI plugin parameters processing module
"""

import json
import os
import sys

import common.params.consts as Consts
from common.logger import logger as Logger

# Logger for the file
logger = None


class K8SParams():
    '''
    Kubernetes specific parameters. Will contain parameters not generic to CNI
    pod_uuid - UUID for the POD. Got from "docker inspect" equivalent
    pod_name - Name of POD got from CNI_ARGS
    pod_namespace - Namespace for the POD got from CNI_ARGS
    pod_pid  - pid for the PODs pause container. Used to map namespace
               pid is needed by the nsenter library used in 'cni' module
    '''

    def __init__(self):
        self.pod_uuid = None
        self.pod_name = None
        self.pod_namespace = None
        self.pod_pid = None

    def set_pod_uuid(self, pod_uuid):
        self.pod_uuid = pod_uuid
        return

    def get_pod_info(self, container_id, pod_uuid=None):
        '''
        Get UUID and PID for POD using "docker inspect" equivalent API
        '''
        from docker import client
        os.environ['DOCKER_API_VERSION'] = '1.22'
        try:
            docker_client = client.Client()
            if docker_client == None:
                raise ParamsError(Consts.PARAMS_ERR_DOCKER_CONNECTION,
                                  'Error creating docker client')
            container = docker_client.inspect_container(container_id)
            self.pod_pid = container['State']['Pid']
            self.pod_uuid = container['Config']['Labels']\
                ['io.kubernetes.pod.uid']
        except:
            # Dont report exception if pod_uuid set from argument already
            # pod-uuid will be specified in argument in case of UT
            if self.pod_uuid == None:
                raise ParamsError(Consts.PARAMS_ERR_GET_UUID,
                                  'Error finding UUID/PID for pod ' +
                                  container_id)
        return

    def get_params(self, container_id=None, json_input=None):
        '''
        In K8S, CNI_ARGS is of format
        "IgnoreUnknown=1;K8S_POD_NAMESPACE=default;\
        K8S_POD_NAME=hello-world-1-81nl8;\
        K8S_POD_INFRA_CONTAINER_ID=<container-id>"
        Get pod-name and infra-container-id from this
        '''
        args = get_env('CNI_ARGS')
        args_list = args.split(";")
        for x in args_list:
            vars_list = x.split('=')
            if vars_list == None:
                continue

            if len(vars_list) >= 2:
                if vars_list[0] == 'K8S_POD_NAMESPACE':
                    self.pod_namespace = vars_list[1]
                if vars_list[0] == 'K8S_POD_NAME':
                    self.pod_name = vars_list[1]
        if self.pod_namespace == None:
            raise ParamsError(CNI_INVALID_ARGS,
                              'K8S_POD_NAMESPACE not set in CNI_ARGS')

        if self.pod_name == None:
            raise ParamsError(CNI_INVALID_ARGS,
                              'K8S_POD_NAME not set in CNI_ARGS')

        # Get UUID and PID for the POD
        self.get_pod_info(container_id)
        return

    def log(self):
        logger.debug('K8SParams pod_uuid = ' + str(self.pod_uuid) +
                     ' pod_pid = ' + str(self.pod_pid) +
                     ' pod_name = ' + str(self.pod_name) +
                     ' pod_namespace = ' + str(self.pod_namespace))
        return

