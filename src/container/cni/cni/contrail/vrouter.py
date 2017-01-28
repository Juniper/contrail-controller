# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
import datetime
import errno
import inspect
import json
import logging
import os
import requests
import sys
import time


"""
VRouter interface module for CNI plugin
Provides following operations to VRouter
- add port
- delete port
- get port
- poll port
A port is identified by vm-uuid
"""


# VRouter error codes
VROUTER_INVALID_DIR = 601
VROUTER_CONN_ERROR = 602
VROUTER_FILE_DELETE_ERROR = 604
VROUTER_FILE_WRITE_ERROR = 605


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


class VRouter():
    # Default VRouter related values
    VROUTER_AGENT_IP = '127.0.0.1'
    VROUTER_AGENT_PORT = 9091
    VROUTER_POLL_TIMEOUT = 3
    VROUTER_POLL_RETRIES = 20

    # Directory containing configuration for the container
    VROUTER_CONFIG_DIR = '/var/lib/contrail/ports/vm'

    def __init__(self, stdin_string):
        self.ip = VRouter.VROUTER_AGENT_IP
        self.port = VRouter.VROUTER_AGENT_PORT
        self.poll_timeout = VRouter.VROUTER_POLL_TIMEOUT
        self.poll_retries = VRouter.VROUTER_POLL_RETRIES
        self.directory = VRouter.VROUTER_CONFIG_DIR
        self.stdin_json = json.loads(stdin_string)

        global logger
        logger = logging.getLogger('vrouter')

        self._get_params_from_stdin()
        return

    def _get_params_from_stdin(self):
        contrail_json = self.stdin_json.get('contrail')
        if contrail_json is None:
            return

        if contrail_json.get('config-dir') is not None:
            self.directory = contrail_json['config-dir']
        if contrail_json.get('vrouter-ip') is not None:
            self.ip = contrail_json['vrouter-ip']
        if contrail_json.get('vrouter-port') is not None:
            self.port = contrail_json['vrouter-port']
        if contrail_json.get('poll-timeout') is not None:
            self.poll_timeout = contrail_json['poll-timeout']
        if contrail_json.get('poll-retries') is not None:
            self.poll_retries = contrail_json['poll-retries']
        return

    def make_filename(self, vm, nw):
        fname = self.directory + '/' + vm
        if nw is not None:
            fname += '/' + nw
        return fname

    def make_url(self, vm, nw, page):
        url = "http://" + self.ip + ":" + str(self.port) + page
        if vm is not None:
            url += "/" + vm

        if nw is not None:
            url += "/" + nw

        headers = {'content-type': 'application/json'}
        return url, headers

    def get_cmd(self, vm, nw, page='/vm'):
        '''
        Get container info from VRouter
        '''
        url, headers = self.make_url(vm, nw, page)
        try:
            r = requests.get(url)
            r.raise_for_status()
            return json.loads(r.text)
        except requests.exceptions.HTTPError as e:
            raise Error(VROUTER_CONN_ERROR,
                        'Error in GET ' + url + ' HTTP Response <' +
                        str(r.status_code) + '> Data : ' + r.text)
        except requests.exceptions.RequestException as e:
            raise Error(VROUTER_CONN_ERROR,
                        'Connection error in GET ' + url +
                        ' Error {}'.format(e))
        except:
            raise Error(VROUTER_CONN_ERROR, 'Unknown error in GET ' + url)
        return

    def poll_cmd(self, vm, nw, page='/vm'):
        '''
        Poll for container info from VRouter
        '''
        i = 0
        while i < self.poll_retries:
            i = i + 1
            try:
                return self.get_cmd(vm, nw, page)
            except Error as vr_err:
                time.sleep(self.poll_timeout)
                # Pass the last exception got at end of polling
                if i == self.poll_retries:
                    raise vr_err
        return

    def get_cfg_cmd(self, vm, nw):
        return self.get_cmd(vm, nw, '/vm-cfg')

    def poll_cfg_cmd(self, vm, nw):
        return self.poll_cmd(vm, nw, '/vm-cfg')

    def delete_file(self, vm, nw):
        '''
        Delete config file for a container
        '''
        fname = self.make_filename(vm, nw)
        if os.path.exists(fname):
            try:
                os.remove(fname)
            except OSError as e:
                raise Error(VROUTER_FILE_DELETE_ERROR,
                            'Error deleting file : ' + fname + '. Error : ' +
                            os.strerror(e.errno))
        return

    def delete_from_vrouter(self, vm, nw):
        '''
        Send request to VRouter to delete a container
        '''
        url, headers = self.make_url(vm, nw, '/vm')
        try:
            # Delete needs vm + nw in request data
            data = {}
            data['vm'] = vm
            if nw is not None:
                data['nw'] = nw
            data_str = json.dumps(data, indent=4)
            r = requests.delete(url, data=data_str, headers=headers)
            r.raise_for_status()
        except requests.exceptions.HTTPError as e:
            raise Error(VROUTER_CONN_ERROR,
                        'Error in DELETE ' + url + ' HTTP Response <' +
                        str(r.status_code) + '> Data : ' + r.text)
        except requests.exceptions.RequestException as e:
            raise Error(VROUTER_CONN_ERROR,
                        'Connection error in DELETE ' + url +
                        ' Error {}'.format(e))
        except:
            raise Error(VROUTER_CONN_ERROR, 'Unknown error in GET ' + url)
        return

    def delete_cmd(self, container_uuid, nw):
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
            raise Error(VROUTER_INVALID_DIR,
                        'Error creating config directory :' + self.directory +
                        '. Error : ' + os.strerror(e.errno))
        return

    def write_file(self, data, vm, nw):
        '''
        Write config file for container. On restart, VRouter will rebuild its
        container-db from these config-files
        '''
        # Ensure directory first
        try:
            os.makedirs(self.directory)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise Error(VROUTER_INVALID_DIR,
                            'Error creating config directory :' +
                            self.directory + '. Error : ' +
                            os.strerror(e.errno))
            if os.path.isdir(self.directory) == False:
                raise Error(VROUTER_INVALID_DIR,
                            'Invalid config directory :' + self.directory +
                            '. Error : ' + os.strerror(e.errno))
        # Write config file
        fname = self.make_filename(vm, nw)
        try:
            f = open(fname, 'w')
            f.write(data)
            f.close()
        except IOError as e:
            raise Error(VROUTER_FILE_WRITE_ERROR,
                        'Error writing file : ' + fname + '. Error : ' +
                        os.strerror(e.errno))
        return

    def add_to_vrouter(self, data, vm, nw):
        '''
        Send ADD message to VRouter
        '''
        url, headers = self.make_url(None, None, '/vm')
        try:
            r = requests.post(url, data=data, headers=headers)
            r.raise_for_status()
        except requests.exceptions.HTTPError as e:
            raise Error(VROUTER_CONN_ERROR,
                        'Error in ADD ' + url + ' HTTP Response <' +
                        str(r.status_code) + '> Data : ' + r.text)
        except requests.exceptions.RequestException as e:
            raise Error(VROUTER_CONN_ERROR,
                        'Connection error in ADD ' + url +
                        ' Error {}'.format(e))
        except:
            raise Error(VROUTER_CONN_ERROR, 'Unknown error in ADD ' + url)
        return

    def add_cmd(self, container_uuid, container_id, container_name,
                container_namespace, host_ifname, container_ifname, nw):
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

    def log(self):
        logger.debug('vrouter-ip = ' + self.ip +
                     ' vrouter-port = ' + str(self.port) +
                     ' poll-timeout = ' + str(self.poll_timeout) +
                     ' poll-retries = ' + str(self.poll_retries) +
                     ' config-dir = ' + self.directory)
        return
