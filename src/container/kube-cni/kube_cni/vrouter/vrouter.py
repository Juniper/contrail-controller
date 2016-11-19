# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
import sys
import time
import requests
import json
import os
import errno
import datetime
import common.logger as Logger

from requests.exceptions import ConnectionError

"""
VRouter interface module for CNI plugin
Provides following operations to VRouter
- add port
- delete port
- get port
- poll port
A port is identified by vm-uuid
"""

# Logger for the file
logger = None

# Error codes
VROUTER_INVALID_DIR = 201
VROUTER_CONN_ERROR = 202
VROUTER_DELETE_ERROR = 203
VROUTER_ADD_ERROR = 204
VROUTER_FILE_DELETE_ERROR = 205
VROUTER_FILE_WRITE_ERROR = 206


class VRouterError(RuntimeError):
    '''
    Exception class for VRouter related errors
    '''

    def __init__(self, code, msg):
        self.code = code
        self.msg = msg
        return

    def log(self):
        logger.error(str(self.code) + ' : ' + self.msg)
        return


class VRouter():

    def __init__(self, ip, port, timeout, retries, directory, log_file,
                 log_level):
        self.ip = ip
        self.port = port
        self.poll_timeout = timeout
        self.poll_retries = retries
        self.directory = directory
        global logger
        logger = Logger.Logger('vrouter', log_file, log_level)
        return

    def make_filename(self, vm, nw=None):
        fname = self.directory + '/' + vm
        if nw != None:
            fname += '/' + nw
        return fname

    def make_url(self, vm, nw=None):
        url = "http://" + self.ip + ":" + str(self.port) + '/vm'
        if vm != None:
            url += "/" + vm

        if nw != None:
            url += "/" + nw

        headers = {'content-type': 'application/json'}
        return url, headers

    def get_cmd(self, vm, nw=None):
        '''
        Get container info from VRouter
        '''
        url, headers = self.make_url(vm, nw)
        try:
            r = requests.get(url)
            if r.status_code != requests.status_codes.codes.ok:
                raise VRouterError(VROUTER_GET_ERROR, 'Error in GET ' + url)
            return json.loads(r.text)
        except:
            raise VRouterError(VROUTER_CONN_ERROR,
                               'Error connecting to ' + url + ' operaion GET')
        return

    def poll_cmd(self, vm, nw=None):
        '''
        Poll for container info from VRouter
        '''
        i = 0
        while i < self.poll_retries:
            i = i + 1
            try:
                resp = self.get_cmd(vm, nw)
                return resp
            except VRouterError as vr_err:
                time.sleep(self.poll_timeout)
                # Pass the last exception got at end of polling
                if i == self.poll_retries:
                    pass
        return

    def delete_file(self, vm, nw=None):
        '''
        Delete config file for a container
        '''
        fname = self.make_filename(vm, nw)
        if os.path.exists(fname):
            try:
                os.remove(fname)
            except OSError as e:
                raise VRouterError(VROUTER_FILE_DELETE_ERROR,
                                   'Error deleting file:' + fname +
                                   '. Error : ' + os.strerror(e.errno))
        return

    def delete_from_vrouter(self, vm, nw=None):
        '''
        Send request to VRouter to delete a container
        '''
        url, headers = self.make_url(vm, nw)
        try:
            # Delete needs vm + nw in request data
            data = {}
            data['vm'] = vm
            if nw != None:
                data['nw'] = nw
            data_str = json.dumps(data, indent=4)
            r = requests.delete(url, data=data_str, headers=headers)
            if r.status_code != requests.status_codes.codes.ok:
                raise VRouterError(VROUTER_DELETE_ERROR,
                                   'Error in Delete ' + url +
                                   ' HTTP Response code ' + r.status_code +
                                   ' HTTP Response Data ' + r.text)
            return
        except:
            raise VRouterError(VROUTER_CONN_ERROR,
                               'Error connecting to ' + url + ' operaion DEL')
        return

    def delete_cmd(self, container_uuid, nw=None):
        '''
        Delete a container.
        First delete the config file for container and then
        Send delete message to VRouter
        '''
        self.delete_file(container_uuid, nw)
        return self.delete_from_vrouter(container_uuid, nw)

    def make_add_data(self, container_uuid, container_id, container_name,
                      container_namespace, host_ifname, container_ifname):
        '''
        Build JSON data for ADD operation
        '''
        data = {}
        data['time'] = str(datetime.datetime.now())
        data['vm-id'] = container_id
        data['vm-uuid'] = container_uuid
        data['host-ifname'] = host_ifname
        data['vm-ifname'] = container_ifname
        data['vm-name'] = container_name
        data['vm-namespace'] = container_namespace
        return json.dumps(data, indent=4)

    def create_directory(self):
        try:
            os.makedirs(self.directory)
        except OSError as e:
            if e.errno == errno.EEXIST and os.path.isdir(self.directory):
                pass
                return
            raise VRouterError(VROUTER_INVALID_DIR,
                               'Error creating config directory :' + self.directory +
                               '. Error : ' + os.strerror(e.errno))
        return

    def write_file(self, data, vm, nw=None):
        '''
        Write config file for container. On restart, VRouter will rebuild its
        container-db from these config-files
        '''
        # Ensure directory first
        try:
            os.makedirs(self.directory)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise VRouterError(VROUTER_INVALID_DIR,
                                   'Error creating config directory :' +
                                   self.directory + '. Error : ' +
                                   os.strerror(e.errno))
            if os.path.isdir(self.directory) == False:
                raise VRouterError(VROUTER_INVALID_DIR,
                                   'Invalid config directory :' +
                                   self.directory + '. Error : ' +
                                   os.strerror(e.errno))
        # Write config file
        fname = self.make_filename(vm, nw)
        try:
            f = open(fname, 'w')
            f.write(data)
            f.close()
        except IOError as e:
            raise VRouterError(VROUTER_FILE_WRITE_ERROR,
                               'Error writing file:' + fname +
                               '. Error : ' + os.strerror(e.errno))
        return

    def add_to_vrouter(self, data, vm, nw=None):
        '''
        Send ADD message to VRouter
        '''
        url, headers = self.make_url(None, None)
        try:
            r = requests.post(url, data=data, headers=headers)
            if r.status_code != requests.status_codes.codes.ok:
                raise VRouterError(VROUTER_ADD_ERROR,
                                   'Error in Add ' + url +
                                   ' HTTP Response code ' + r.status_code +
                                   ' HTTP Response Data ' + r.text)
            return
        except:
            raise VRouterError(VROUTER_CONN_ERROR,
                               'Error connecting to ' + url + ' operaion POST')

    def add_cmd(self, container_uuid, container_id, container_name,
                container_namespace, host_ifname, container_ifname, nw=None):
        '''
        Add a container.
        First add the config file for container and then
        Send add message to VRouter
        '''
        data = self.make_add_data(container_uuid, container_id, container_name,
                                  container_namespace, host_ifname,
                                  container_ifname)
        self.write_file(data, container_uuid, nw)
        self.add_to_vrouter(data, container_uuid, nw)
        return self.poll_cmd(container_uuid, nw)
