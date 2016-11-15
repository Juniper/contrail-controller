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

import sys
sys.path.insert(0, '/root/kube_cni')
sys.path.insert(0, '/usr/lib/python2.7/dist-packages')
import os
import json

# Error codes from params module
PARAMS_ERR_ENV = 101
PARAMS_ERR_DOCKER_CONNECTION = 102
PARAMS_ERR_GET_UUID = 103

# Default VRouter related values
VROUTER_AGENT_IP = '127.0.0.1'
VROUTER_AGENT_PORT = 9091
VROUTER_POLL_TIMEOUT = 3
VROUTER_POLL_RETRIES= 20

# Container mode. Can only be k8s
CONTRAIL_CONTAINER_MODE = "k8s"
CONTRAIL_CONTAINER_MTU = 1500
CONTRAIL_CONFIG_DIR = '/opt/contrail/ports'

# Default K8S Pod related values
POD_DEFAULT_MTU = 1500

# Exception class for params related errors
class ParamsError(RuntimeError):
    def __init__(self, code, msg):
        self.msg = msg
        self.code = code
        return

    def Log(self, logger):
        logger.error('Params %d - %s', code, self.msg)
        return

# Helper function to get environment variable
def GetEnv(key):
    val = os.environ.get(key)
    if val == None:
        raise ParamsError(PARAMS_ERR_ENV, 'Missing environment variable ' + key)
    return val

# Contrail 
# - mode      : Only kubenrnetes supported for now (k8s)
# - mtu       : MTU to be configured for interface inside container
# - conf_dir  : Plugin will store the Pod configuration in this directory.
#               The VRouter agent will scan this directore on restart
# - vrouter_ip : IP address where VRouter agent is running
# - vrouter_port : Port on which VRouter agent is running
# - poll_timeout : Timeout for the GET request to VRouter
# - poll_retries : Number of retries for GET request to VRouter
class ContrailParams():
    def __init__(self):
        self.mode = CONTRAIL_CONTAINER_MODE
        self.mtu = CONTRAIL_CONTAINER_MTU
        self.dir = CONTRAIL_CONFIG_DIR
        self.vrouter_ip = VROUTER_AGENT_IP
        self.vrouter_port = VROUTER_AGENT_PORT
        self.poll_timeout = VROUTER_POLL_TIMEOUT
        self.poll_retries = VROUTER_POLL_RETRIES
        return

    def Get(self, json_input = None):
        if json_input.get('config-dir') != None:
            self.dir = json_input['config-dir']
        if json_input.get('vrouter-ip') != None:
            self.vrouter_ip = json_input['vrouter-ip']
        if json_input.get('vrouter-port') != None:
            self.vrouter_port = json_input['vrouter-port']
        if json_input.get('poll-timeout') != None:
            self.poll_timeout = json_input['poll-timeout']
        if json_input.get('poll-retries') != None:
            self.poll_timeout = json_input['poll-retries']
        return

    def Log(self, logger):
        logger.log('ContrailParams mode = ', self.mode, ' mtu = %d', self.mtu,
                   ' config-dir = ', self.dir)
        logger.log('ContrailParams vrouter-ip = ', self.vrouter_ip,
                ' vrouter-port = ', self.vrouter_port,
                ' poll-timeout = ', self.poll_timeout,
                ' poll-retries = ', self.poll_retries)
        return

# Kubernetes specific parameters. Will contain parameters not generic to CNI
# pod_uuid - UUID for the POD. Got from "docker inspect" equivalent
# pod_name - Name of POD got from CNI_ARGS
# pod_namespace - Namespace for the POD got from CNI_ARGS
class K8SParams():
    def __init__(self):
        self.pod_uuid = None
        self.pod_name = None
        self.pod_namespace = None
        self.pod_pid = 0
        return

    # Get UUID and PID for POD. Uses "docker inspect" equivalent API
    def GetPodInfo(self, container_id):
        from docker import client
        os.environ['DOCKER_API_VERSION'] = '1.22'
        try:
            docker_client = client.Client()
            if docker_client == None:
                raise ParamsError(PARAMS_ERR_DOCKER_CONNECTION,
                                'Error creating docker client')
            container = docker_client.inspect_container(container_id)
            self.pod_uuid = container['Config']['Labels']\
                            ['io.kubernetes.pod.uid']
            self.pod_pid = container['State']['Pid']
        except:
            raise ParamsError(PARAMS_ERR_GET_UUID,
                            'Error finding UUID/PID for pod ' + container_id)
        return

    def Get(self, container_id = None, json_input = None):
        # In K8S, CNI_ARGS is of format
        # "IgnoreUnknown=1;K8S_POD_NAMESPACE=default;\
        # K8S_POD_NAME=hello-world-1-81nl8;\
        # K8S_POD_INFRA_CONTAINER_ID=<container-id>"
        # Get pod-name and infra-container-id from this
        args = GetEnv('CNI_ARGS')
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
        self.GetPodInfo(container_id)
        return

    def Log(self, logger):
        logger.log('K8SParams pod_uuid = ', self.pod_uuid,
                ' pod_pid = ', self.pod_pid,
                ' pod_name = ', self.pod_name,
                ' pod_namespace = ', self.pod_namespace)
        return

# Top level class holding all arguments relavent to CNI
# - k8s_params       : Contains kubernetes specific arguements
# - contrail_params  : Contains contrail specific arguments needed for CNI
# - container_id     : Identifier for the container
# - container_ifname : Name of interface inside the container
# - container_netns  : Network namespace for the container
# - command          : CNI command for the operation
class Params():
    def __init__(self, logger):
        self.contrail_params = ContrailParams()
        self.k8s_params = K8SParams()
        self.container_id = None
        self.container_ifname = None
        self.container_netns = None
        self.command = None
        self.logger = logger
        return

    def Get(self, json_input = None):
        self.command = GetEnv('CNI_COMMAND')
        self.container_id = GetEnv('CNI_CONTAINERID')
        self.container_netns = GetEnv('CNI_NETNS')
        self.container_ifname = GetEnv('CNI_IFNAME')
        self.k8s_params.Get(self.container_id, json_input.get('k8s'))
        self.contrail_params.Get(json_input.get('contrail'))
        return

    def Log(self):
        self.logger.log('Params container-id = ', self.container_id,
                ' container-ifname = ', self.container_ifname,
                ' continer-netns = ', self.container_netns)
        self.k8s_params.Log(self.logger)
        self.contrail_params.Log(self.logger)
        return
