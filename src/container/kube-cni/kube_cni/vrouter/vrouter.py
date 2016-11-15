# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
#Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
import requests
import json
import os
import errno
import datetime

from requests.exceptions import ConnectionError

"""
VRouter interface module for CNI plugin
"""
# Error codes
VROUTER_INVALID_DIR = 201
VROUTER_CONN_ERROR = 202
VROUTER_DELETE_ERROR = 203
VROUTER_ADD_ERROR = 204
VROUTER_FILE_DELETE_ERROR = 205
VROUTER_FILE_WRITE_ERROR = 206

# Exception class for VRouter related errors
class VRouterError(RuntimeError):
    def __init__(self, code, msg):
        self.code = code
        self.msg = msg
        return

    def Log(self, logger):
        logger.error('VRouter %d : %s', self.code, self.msg)
        return

class VRouter():
    def __init__(self, handle, ip, port, timeout, retries, directory):
        self.handle = handle
        self.ip = ip
        self.port = port
        self.poll_timeout = timeout
        self.poll_retries = retries
        self.dir = directory
        return

    def MakeFileName(self, vm, nw = None):
        fname = self.dir + '/port-' + vm
        if nw != None:
            fname += '-' + nw
        return fname

    def MakeUrl(self, vm, nw = None):
        url = "http://" + self.ip + ":" + str(self.port) + '/port'
        if vm != None:
            url += "/" + vm

        if nw != None:
            url += "/" + nw

        headers = {'content-type': 'application/json'}
        return url, headers

    # Get container info from VRouter
    def Get(self, vm, nw = None):
        url, headers = self.MakeUrl(vm, nw)
        try:
            r = requests.get(url)
            if r.status_code != 200:
                raise VRouterError(VROUTER_GET_ERROR, 'Error in GET ' + url)
            return json.loads(r.text)
        except:
            raise VRouterError(VROUTER_CONN_ERROR,
                               'Error connecting to VRouter')
        return

    # Poll for container info from VRouter
    def Poll(self, vm, nw = None):
        i = 0
        while i < self.poll_retries:
            i = i + 1
            try:
                resp = self.Get(vm, nw)
                return resp
            except VRouter.VRouterError as vr_err:
                sleep(self.poll_timeout)
                # Pass the last exception got at end of polling
                if i == self.poll_retries:
                    pass
        return

    # Delete config file for a container
    def DeleteFile(self, vm, nw = None):
        fname = self.MakeFileName(vm, nw)
        if os.path.exists(fname):
            try:
                os.remove(fname)
            except OSError as e:
                raise VRouterError(VROUTER_FILE_DELETE_ERROR,
                                   'Error deleting file:' + fname +\
                                   '. Error : ' + os.strerror(e.errno))
        return

    # Send request to VRouter to delete a container
    def DeleteFromVRouter(self, vm, nw = None):
        url, headers = self.MakeUrl(vm, nw)
        try:
            # Delete needs vm + nw in request data
            data = {}
            data['vm'] = vm
            if nw != None:
                data['nw'] = nw
            data_str = json.dumps(data, indent=4)
            r = requests.delete(url, data=data_str, headers=headers)
            if r.status_code != 200:
                raise VRouterError(VROUTER_DELETE_ERROR,
                                   'Error in Delete ' + url +\
                                   ' HTTP Response code ' + r.status_code +\
                                   ' HTTP Response Data ' + r.text)
            return
        except:
            raise VRouterError(VROUTER_CONN_ERROR,
                               'Error connecting to VRouter ' + url)
        return

    # Delete a container.
    # First delete the config file for container and then
    # Send delete message to VRouter
    def Delete(self, container_uuid, nw = None):
        self.DeleteFile(container_uuid, nw)
        return self.DeleteFromVRouter(container_uuid, nw)

    # Build JSON data for ADD operation
    def MakeAddData(self, container_uuid, container_id, container_name,
                    container_namespace, host_ifname, container_ifname):
        data = {}
        data['time'] = str(datetime.datetime.now())
        data['vm'] = container_id
        data['vm_uuid'] = container_uuid
        data['host_ifname'] = host_ifname
        data['container_ifname'] = container_ifname
        data['container_name'] = container_name
        data['container_namespace'] = container_namespace
        return json.dumps(data, indent=4)

    def CreateDirectory(self):
        try:
            os.makedirs(self.dir)
        except OSError as e:
            if e.errno == errno.EEXIST and os.path.isdir(self.dir):
                pass
                return
            raise VRouterError(VROUTER_INVALID_DIR,
                               'Error creating config directory :' + self.dir +\
                               '. Error : ' + os.strerror(e.errno))
        return

    # Write config file for container. On restart, VRouter will rebuild its
    # container-db from these config-files
    def WriteFile(self, data, vm, nw = None):
        # Ensure directory first
        try:
            os.makedirs(self.dir)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise VRouterError(VROUTER_INVALID_DIR,
                        'Error creating config directory :' + self.dir +\
                        '. Error : ' + os.strerror(e.errno))
            if os.path.isdir(self.dir) == False:
                raise VRouterError(VROUTER_INVALID_DIR,
                        'Invalid config directory :' + self.dir +\
                        '. Error : ' + os.strerror(e.errno))
        # Write config file
        fname = self.MakeFileName(vm, nw)
        try:
            f = open(fname, 'w')
            f.write(data)
            f.close()
        except IOError as e:
            raise VRouterError(VROUTER_FILE_WRITE_ERROR,
                               'Error writing file:' + fname +\
                               '. Error : ' + os.strerror(e.errno))
        return

    # Send ADD message to VRouter
    def AddToVRouter(self, data, vm, nw = None):
        url, headers = self.MakeUrl(None, None)
        try:
            r = requests.post(url, data=data, headers=headers)
            if r.status_code != 200:
                raise VRouterError(VROUTER_ADD_ERROR,
                                   'Error in Add ' + url +\
                                   ' HTTP Response code ' + r.status_code +\
                                   ' HTTP Response Data ' + r.text)
            return
        except:
            raise VRouterError(VROUTER_CONN_ERROR,
                               'Error connecting to VRouter ' + url)

    # Add a container.
    # First add the config file for container and then
    # Send add message to VRouter
    def Add(self, container_uuid, container_id, container_name,
            container_namespace, host_ifname, container_ifname, nw = None):
        data = self.MakeAddData(container_uuid, container_id, container_name,
                                container_namespace, host_ifname,
                                container_ifname)
        self.WriteFile(data, container_uuid, nw)
        self.AddToVRouter(data, container_uuid, nw)
        return self.Poll(container_uuid, nw)
