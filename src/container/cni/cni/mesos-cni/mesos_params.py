
# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
MESOS CNI plugin parameters processing module
"""

import json
import os
import sys

from cni.common.params import consts as Consts
from cni.common.params import params as Params
from cni.common.logger import logger as Logger

# Logger for the file
logger = None


class MESOSParams(Params.Params):
    '''
    MESOS specific parameters. Will contain parameters not generic to CNI
    '''

    def __init__(self):
        Params.Params.__init__(self)
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

        self.get_common_params(json_input)

        # Get UUID and PID for the Container
        self.get_pod_info(self.container_id)
        return

    def get_stdin_params(stdin_json):
        self.mesos_params = stdin_json
        return

    def log(self):
        logger.debug('MESOS Params pod_uuid = ' + str(self.pod_uuid) +
                     ' pod_pid = ' + str(self.pod_pid) +
                     ' pod_name = ' + str(self.pod_name) +
                     ' pod_namespace = ' + str(self.pod_namespace))
        return

