# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
CNI plugin core module. Provides method used to implement CNI plugin

Reads necessary parameters from two places,
    - Environment variables
    - STDIN

The module defines parameters complying to CNI specs. Further CNI processing
like creating interface and configuring interface will be using these
parameters
"""


import json
import logging
import os
import sys


# logger for the file
logger = None


# Params error codes
CNI_ERR_ENV = 101


class Error(RuntimeError):
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


class Cni():
    '''
    CNI implementation class. Contains following information,
    - command          : CNI command for the operation
    - container_id     : Identifier for the container
    - container_ifname : Name of interface inside the container
    - container_name   : Name of container
    - container_netns  : Network namespace for the container
    - container_uuid   : UUID of the container
    - container_vn     : Virtual Network for the container. Unused for now
    - stdin_string     : Data passed as stdin
    - stdin_json       : stdin data in form on json
    '''

    # CNI Version supported
    CNI_VERSION = '0.2.0'

    # CNI commands supported
    CNI_CMD_VERSION = 'version'
    CNI_CMD_ADD = 'add'
    CNI_CMD_DEL = 'del'
    CNI_CMD_DELETE = 'delete'

    def __init__(self, stdin_string):
        # Logger for the file
        global logger
        logger = logging.getLogger('cni')

        self.command = None
        self.container_id = None
        self.container_ifname = None
        self.container_name = None
        self.container_netns = None
        self.container_uuid = None
        self.container_vn = None
        self.stdin_string = stdin_string
        self.stdin_json = None

        self._get_params()
        return

    def _get_params(self):
        '''
        Get CNI arguments. The arguments can be from 2 inputs,
           - Environment variables : CNI passes a set of environment variables
           - STDIN                 : CNI passes config file as stdin
        '''
        # Read and process parameters from environment variables
        self._get_params_from_env()
        # Read and process parameters from stdin
        self._get_params_from_stdin()
        return

    # Read config file from STDIN or optionally from a file
    def _get_params_from_stdin(self):
        self.stdin_json = json.loads(self.stdin_string)
        return

    @staticmethod
    def get_env(key):
        '''
        Helper function to get environment variable. Throws exception if
        environment is not found
        '''
        val = os.environ.get(key)
        if val is None:
            raise Error(CNI_ERR_ENV, 'Missing environment variable ' + key)
        return val

    def _get_params_from_env(self, json_input=None):
        self.command = self.get_env('CNI_COMMAND')
        # We dont expect any arguments for 'version'
        if self.command.lower() == 'version':
            return

        # Parse arguments needed for add/del/delete/get/poll
        # get/del may not strictly need all parameters, but having common
        # code for now
        self.container_id = self.get_env('CNI_CONTAINERID')
        self.container_ifname = self.get_env('CNI_IFNAME')
        self.container_netns = self.get_env('CNI_NETNS')
        return

    # Some of the fields in Params are not derivable from standard CNI
    # arguments. "update" is used to set such variables
    def update(self, uuid, name, vn=None):
        if uuid is not None:
            self.container_uuid = uuid
        if name is not None:
            self.container_name = name
        else:
            self.container_name = uuid
        if vn is not None:
            self.container_vn = vn
        return

    # Log class parameters
    def log(self):
        # Log environment variables
        for env in os.environ.keys():
            logger.debug(env + '=' + os.environ[env])

        data = json.dumps(self.stdin_json, indent=4)
        logger.debug('Params ' +
                     ' command = ' + self.command +
                     ' container-id = ' + str(self.container_id) +
                     ' container-ifname = ' + str(self.container_ifname) +
                     ' continer-name = ' + str(self.container_name) +
                     ' continer-netns = ' + str(self.container_netns) +
                     ' continer-uuid = ' + str(self.container_uuid) +
                     ' continer-vn = ' + str(self.container_vn) +
                     ' stdin-data = ' + data)
        return

    @staticmethod
    def error_exit(code, msg):
        '''
        Report CNI error and exit
        '''
        resp = {}
        resp['cniVersion'] = Cni.CNI_VERSION
        resp['code'] = code
        resp['msg'] = msg
        json_data = json.dumps(resp, indent=4)
        logger.error('CNI Error : ' + json_data)
        print json_data
        sys.exit(code)
        return

    @staticmethod
    def _make_cni_response(resp):
        '''
        Make CNI response from json in resp
        '''
        json_data = json.dumps(resp, indent=4)
        logger.debug('CNI output : ' + json_data)
        print json_data
        return

    @staticmethod
    def build_response(ip4_address, plen, gateway, dns_server):
        '''
        CNI response from dict in data
        '''
        resp = {}
        resp['cniVersion'] = Cni.CNI_VERSION
        ip4 = {}
        if ip4_address is not None:
            dns = {}
            dns['nameservers'] = [dns_server]
            resp['dns'] = dns

            ip4['gateway'] = gateway
            ip4['ip'] = ip4_address + '/' +  str(plen)
            route = {}
            route['dst'] = '0.0.0.0/0'
            route['gw'] = gateway
            routes = [route]
            ip4['routes'] = routes
            resp['ip4'] = ip4
        Cni._make_cni_response(resp)
        return resp

    @staticmethod
    def delete_response():
        '''
        CNI response from delete command
        '''
        resp = {}
        resp['cniVersion'] = Cni.CNI_VERSION
        resp['code'] = 0
        resp['msg'] = 'Delete passed'
        Cni._make_cni_response(resp)
        return resp

    @staticmethod
    def version_response():
        '''
        CNI response from version command
        '''
        resp = {}
        resp['cniVersion'] = Cni.CNI_VERSION
        resp['supportedVersions'] = [Cni.CNI_VERSION]
        Cni._make_cni_response(resp)
        return resp
