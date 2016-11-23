# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
CNI plugin parameters processing module
Parameters are defined in 3 different classes
- ContrailParams : Contains contrain specific parameters
- K8SParams : Contains kubernetes specific parameters
- CniParams : Contains CNI defined parameters
              Also holds ContrailParams + K8SParams
"""

import json
import os
import sys

import consts as Consts
import kube_cni.common.logger as Logger

# Logger for the file
logger = None


def get_env(key):
    '''
    Helper function to get environment variable
    '''
    val = os.environ.get(key)
    if val == None:
        raise ParamsError(Consts.PARAMS_ERR_ENV,
                          'Missing environment variable ' + key)
    return val


class ParamsError(RuntimeError):
    '''
    Exception class for params related errors
    '''

    def __init__(self, code, msg):
        self.msg = msg
        self.code = code
        return

    def log(self):
        logger.error(str(self.code) + ' : ' + self.msg)
        return


class ContrailParams():
    '''
    Contrail specific parameters
    - mode      : Only kubenrnetes supported for now (k8s)
    - conf_dir  : Plugin will store the Pod configuration in this directory.
                  The VRouter agent will scan this directore on restart
    - vrouter_ip : IP address where VRouter agent is running
    - vrouter_port : Port on which VRouter agent is running
    - poll_timeout : Timeout for the GET request to VRouter
    - poll_retries : Number of retries for GET request to VRouter
    '''

    def __init__(self):
        self.mode = Consts.CONTRAIL_CONTAINER_MODE
        self.directory = Consts.CONTRAIL_CONFIG_DIR
        self.vrouter_ip = Consts.VROUTER_AGENT_IP
        self.vrouter_port = Consts.VROUTER_AGENT_PORT
        self.poll_timeout = VROUTER_POLL_TIMEOUT
        self.poll_retries = VROUTER_POLL_RETRIES
        self.log_file = Consts.LOG_FILE
        self.log_level = Consts.LOG_LEVEL
        return

    def get_params(self, json_input=None):
        if json_input == None:
            return
        if json_input.get('config-dir') != None:
            self.directory = json_input['config-dir']
        if json_input.get('vrouter-ip') != None:
            self.vrouter_ip = json_input['vrouter-ip']
        if json_input.get('vrouter-port') != None:
            self.vrouter_port = json_input['vrouter-port']
        if json_input.get('poll-timeout') != None:
            self.poll_timeout = json_input['poll-timeout']
        if json_input.get('poll-retries') != None:
            self.poll_timeout = json_input['poll-retries']
        return

    def get_loggin_params(self, json_input):
        if json_input == None:
            return
        if json_input.get('log-file') != None:
            self.log_file = json_input['log-file']
        if json_input.get('log-level') != None:
            self.log_level = json_input['log-level']
        return

    def log(self):
        logger.debug('mode = ' + self.mode + ' config-dir = ' + self.directory)
        logger.debug('vrouter-ip = ' + self.vrouter_ip +
                     ' vrouter-port = ' + str(self.vrouter_port) +
                     ' poll-timeout = ' + str(self.poll_timeout) +
                     ' poll-retries = ' + str(self.poll_retries))
        return


class Params():
    '''
    Top level class holding all arguments relavent to CNI
    - command          : CNI command for the operation
    - k8s_params       : Contains kubernetes specific arguements
    - contrail_params  : Contains contrail specific arguments needed for CNI
    - container_id     : Identifier for the container
    - container_ifname : Name of interface inside the container
    - container_netns  : Network namespace for the container
    '''

    def __init__(self):
        self.command = None
        self.k8s_params = K8SParams()
        self.contrail_params = ContrailParams()
        self.container_id = None
        self.container_ifname = None
        self.container_netns = None
        return

    def get_loggin_params(self, json_input):
        self.contrail_params.get_loggin_params(json_input.get('contrail'))
        global logger
        logger = Logger.Logger('params', self.contrail_params.log_file,
                               self.contrail_params.log_level)

    def get_params(self, json_input=None):
        self.command = get_env('CNI_COMMAND')
        self.container_id = get_env('CNI_CONTAINERID')
        self.container_netns = get_env('CNI_NETNS')
        self.container_ifname = get_env('CNI_IFNAME')
        self.contrail_params.get_params(json_input.get('contrail'))
        self.k8s_params.get_params(self.container_id, json_input.get('k8s'))
        return

    def log(self):
        logger.debug('Params container-id = ' + self.container_id +
                     ' container-ifname = ' + self.container_ifname +
                     ' continer-netns = ' + self.container_netns)
        self.k8s_params.log()
        self.contrail_params.log()
        return
