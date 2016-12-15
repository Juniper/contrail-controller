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

import inspect
import os
import sys

# set parent directory in sys.path
current_file = os.path.abspath(inspect.getfile(inspect.currentframe()))  # nopep8
sys.path.append(os.path.dirname(os.path.dirname(current_file)))  # nopep8
from common import logger as Logger

# Logger for the file
logger = None

# Error codes from params module
PARAMS_ERR_ENV = 101
PARAMS_ERR_DOCKER_CONNECTION = 102
PARAMS_ERR_GET_UUID = 103
PARAMS_ERR_GET_PID = 104
PARAMS_ERR_INVALID_CMD = 105

# Default VRouter related values
VROUTER_AGENT_IP = '127.0.0.1'
VROUTER_AGENT_PORT = 9091
VROUTER_POLL_TIMEOUT = 3
VROUTER_POLL_RETRIES = 20

# Container mode. Can only be k8s
CONTRAIL_CNI_MODE_K8S = "k8s"
CONTRAIL_CNI_MODE_CONTRAIL_K8S = "contrail-k8s"
CONTRAIL_PARENT_INTERFACE = "eth0"
CONTRAIL_CONTAINER_MTU = 1500
CONTRAIL_CONFIG_DIR = '/var/lib/contrail/ports/vm'

# Default K8S Pod related values
POD_DEFAULT_MTU = 1500

# Logging parameters
LOG_FILE = '/var/log/contrail/cni/opencontrail.log'
LOG_LEVEL = 'WARNING'


def get_env(key):
    '''
    Helper function to get environment variable
    '''
    val = os.environ.get(key)
    if val is None:
        raise ParamsError(PARAMS_ERR_ENV,
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
    - mode      : CNI mode. Can take following values,
                  - k8s : Kubernetes running on baremetal
                  - nested-k8s : Kubernetes running on a VM. The container
                                 interfaces are managed by VRouter running
                                 on orchestrator of VM
    - parent_interface : Field valid only when mode is "nested-k8s".
                         Specifies name of interface inside the VM.
                         Container interfaces are created as sub-interface over
                         this interface
    - conf_dir  : Plugin will store the Pod configuration in this directory.
                  The VRouter agent will scan this directore on restart
    - vrouter_ip : IP address where VRouter agent is running
    - vrouter_port : Port on which VRouter agent is running
    - poll_timeout : Timeout for the GET request to VRouter
    - poll_retries : Number of retries for GET request to VRouter
    '''

    def __init__(self):
        self.mode = CONTRAIL_CNI_MODE_K8S
        self.parent_interface = CONTRAIL_PARENT_INTERFACE
        self.directory = CONTRAIL_CONFIG_DIR
        self.vrouter_ip = VROUTER_AGENT_IP
        self.vrouter_port = VROUTER_AGENT_PORT
        self.poll_timeout = VROUTER_POLL_TIMEOUT
        self.poll_retries = VROUTER_POLL_RETRIES
        self.log_file = LOG_FILE
        self.log_level = LOG_LEVEL
        return

    @staticmethod
    def parse_mode(mode):
        if mode.lower() == CONTRAIL_CNI_MODE_K8S:
            return CONTRAIL_CNI_MODE_K8S
        if mode.lower() == CONTRAIL_CNI_MODE_CONTRAIL_K8S:
            return CONTRAIL_CNI_MODE_CONTRAIL_K8S
        return CONTRAIL_CNI_MODE_K8S

    def get_params(self, json_input=None):
        if json_input is None:
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
            self.poll_retries = json_input['poll-retries']
        if json_input.get('mode') != None:
            self.mode = self.parse_mode(json_input['mode'])
        if json_input.get('parent-interface') != None:
            self.parent_interface = json_input['parent-interface']
        return

    def get_loggin_params(self, json_input):
        if json_input is None:
            return
        if json_input.get('log-file') != None:
            self.log_file = json_input['log-file']
        if json_input.get('log-level') != None:
            self.log_level = json_input['log-level']
        return

    def log(self):
        logger.debug('mode = ' + self.mode + ' config-dir = ' + self.directory +
                     ' parent-interface = ' + self.parent_interface)
        logger.debug('vrouter-ip = ' + self.vrouter_ip +
                     ' vrouter-port = ' + str(self.vrouter_port) +
                     ' poll-timeout = ' + str(self.poll_timeout) +
                     ' poll-retries = ' + str(self.poll_retries))
        return


class K8SParams():
    '''
    Kubernetes specific parameters. Will contain parameters not generic to CNI
    pod_uuid - UUID for the POD. Got from "docker inspect" equivalent
    pod_name - Name of POD got from CNI_ARGS
    pod_namespace - Namespace for the POD got from CNI_ARGS
    pod_pid  - pid for the PODs pause container.
               pid is needed by 'cni' module in creating veth interfaces
    '''

    def __init__(self):
        self.pod_uuid = None
        self.pod_name = None
        self.pod_namespace = None
        self.pod_pid = None

    def set_pod_uuid(self, pod_uuid):
        self.pod_uuid = pod_uuid
        return

    def set_pod_pid(self, pod_pid):
        self.pod_pid = pod_pid
        return

    def get_pod_info(self, container_id, pod_uuid=None):
        '''
        Get UUID and PID for POD using "docker inspect" equivalent API
        '''
        from docker import client
        os.environ['DOCKER_API_VERSION'] = '1.22'
        try:
            docker_client = client.Client()
            if docker_client is None:
                raise ParamsError(PARAMS_ERR_DOCKER_CONNECTION,
                                  'Error creating docker client')
            container = docker_client.inspect_container(container_id)
            self.pod_pid = container['State']['Pid']
            self.pod_uuid = \
                container['Config']['Labels']['io.kubernetes.pod.uid']
        except:
            # Dont report exception if pod_uuid set from argument already
            # pod-uuid will be specified in argument in case of UT
            if self.pod_uuid is None:
                raise ParamsError(PARAMS_ERR_GET_UUID,
                                  'Error finding UUID for pod ' +
                                  container_id)
            if self.pod_pid is None:
                raise ParamsError(PARAMS_ERR_GET_PID,
                                  'Error finding PID for pod ' +
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
            if vars_list is None:
                continue

            if len(vars_list) >= 2:
                if vars_list[0] == 'K8S_POD_NAMESPACE':
                    self.pod_namespace = vars_list[1]
                if vars_list[0] == 'K8S_POD_NAME':
                    self.pod_name = vars_list[1]
        if self.pod_namespace is None:
            raise ParamsError(CNI_INVALID_ARGS,
                              'K8S_POD_NAMESPACE not set in CNI_ARGS')

        if self.pod_name is None:
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
        arg_cmds = ['get', 'poll', 'add', 'delete', 'del']

        if self.command.lower() == 'version':
            return

        if self.command.lower() in arg_cmds:
            self.container_id = get_env('CNI_CONTAINERID')
            self.container_netns = get_env('CNI_NETNS')
            self.container_ifname = get_env('CNI_IFNAME')
            self.contrail_params.get_params(json_input.get('contrail'))
            self.k8s_params.get_params(self.container_id, json_input.get('k8s'))
            return
        else:
            raise ParamsError(PARAMS_ERR_INVALID_CMD, 'Invalid command : ' +
                              self.command)
        return

    def log(self):
        logger.debug('Params container-id = ' + str(self.container_id) +
                     ' container-ifname = ' + str(self.container_ifname) +
                     ' continer-netns = ' + str(self.container_netns))
        self.k8s_params.log()
        self.contrail_params.log()
        return
