# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
CNI implementation
Demultiplexes on the CNI_COMMAND and runs the necessary operation
"""

import ctypes
import errno
import inspect
import json
import os
import sys

from pyroute2 import NetlinkError
from pyroute2 import IPRoute

# set parent directory in sys.path
current_file = os.path.abspath(inspect.getfile(inspect.currentframe()))
sys.path.append(os.path.dirname(os.path.dirname(current_file)))
from common import logger as Logger
from params import params as Params

# logger for the file
logger = None

# CNI commands
CNI_CMD_VERSION = 'version'
CNI_CMD_GET = 'get'
CNI_CMD_POLL = 'poll'
CNI_CMD_ADD = 'add'
CNI_CMD_DELETE = 'delete'

# CNI version supported
CNI_VERSION = '0.2.0'

CNI_UNSUPPORTED_CMD = 301
CNI_ERROR_DEL_VETH = 302
CNI_ERROR_ADD_VETH = 303
CNI_ERROR_CONFIG_VETH = 304
CNI_ERROR_NS_ENTER = 305
CNI_ERROR_NS_LEAVE = 306


def ErrorExit(logger, code, msg):
    '''
    Report CNI error and exit
    '''
    resp = {}
    resp['cniVersion'] = CNI_VERSION
    resp['code'] = code
    resp['msg'] = msg
    json_data = json.dumps(resp, indent=4)
    logger.error('CNI Error : ' + json_data)
    print json_data
    sys.exit(code)
    return


class CniError(RuntimeError):
    '''
    Exception class for CNI related errors
    '''

    def __init__(self, code, msg):
        self.msg = msg
        self.code = code
        return

    def log(self):
        logger.error(str(self.code) + ' : ' + self.msg)
        return

class CniNamespace(object):
    '''
    Helper class to configure interface inside a network-namespace
    The class must be used using 'with' statement as follows,
    with CniNamespace('/proc/<pid>/ns/net'):
        do_something()
    The namespace is restored at end
    '''
    def __init__(self, ns_path, logger):
        self.libc = ctypes.CDLL('libc.so.6', use_errno=True)
        self.my_path = '/proc/self/ns/net'
        self.my_fd = os.open(self.my_path, os.O_RDONLY)
        self.ns_path = ns_path
        self.ns_fd = os.open(self.ns_path, os.O_RDONLY)
        self.logger = logger
        return

    def close_files(self):
        if self.ns_fd is not None:
            os.close(self.ns_fd)
            self.ns_fd = None

        if self.my_fd is not None:
            os.close(self.my_fd)
            self.my_fd = None
        return

    def __enter__(self):
        self.logger.debug('Entering namespace <' + self.ns_path + '>')
        # Enter the namespace
        if self.libc.setns(self.ns_fd, 0) == -1:
            e = ctypes.get_errno()
            self.close_files()
            raise CniError(CNI_ERROR_NS_ENTER,
                           'Error entering namespace ' + self.ns_path +
                           '. Error ' + str(e) + ' : ' + errno.errorcode[e])
        return

    def __exit__(self, type, value, tb):
        self.logger.debug('Leaving namespace <' + self.ns_path + '>')
        if self.libc.setns(self.my_fd, 0) == -1:
            e = ctypes.get_errno()
            self.close_files()
            raise CniError(CNI_ERROR_NS_LEAVE,
                           'Error leaving namespace ' + self.ns_path +
                           '. Error ' + str(e) + ' : ' + errno.errorcode[e])
        self.close_files()
        return

class CniNetns():

    def __init__(self, host_ifname, container_ifname, container_netns):
        self.main_pid = os.getppid()
        self.container_netns = container_netns
        self.host_ifname = host_ifname
        self.container_ifname = container_ifname
        return

    # Delete the veth pair
    def delete_veth(self):
        ip = IPRoute()
        iface = ip.link_lookup(ifname=self.host_ifname)
        if len(iface) == 0:
            return
        idx = iface[0]
        try:
            ip.link('del', index=idx)
        except NetlinkError as e:
            raise CniError(CNI_ERROR_DEL_VETH,
                           'Error deleting veth device ' + self.host_ifname +
                           ' code ' + str(e.code) + ' message ' + e.message)

        return

    def create_veth(self):
        '''
        Create veth pair
        The ifname inside container can be same for multiple containers.
        Hence, the veth pair cannot be created inside the host namespace.
        Create veth pair inside container and move one-end to host namespace
        '''
        ip = IPRoute()
        iface = ip.link_lookup(ifname=self.host_ifname)
        if len(iface) != 0:
            return

        # Switch to container namespace
        with CniNamespace(self.container_netns, logger):
            ip_ns = IPRoute()
            try:
                # Create veth pairs
                ip_ns.link_create(ifname=self.host_ifname,
                                  peer=self.container_ifname, kind='veth')
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    raise CniError(CNI_ERROR_ADD_VETH,
                                   'Error creating veth device ' +
                                   self.host_ifname + ' code ' + str(e.code) +
                                   ' message ' + e.message)
            # Move one end of veth pair to host network namespace
            idx = ip_ns.link_lookup(ifname=self.host_ifname)[0]
            ip_ns.link('set', index=idx, net_ns_pid=self.main_pid)
        return

    def veth_error(self, e, ifname, message):
        raise CniError(CNI_ERROR_CONFIG_VETH,
                       message + ifname + ' code ' + str(e.code) +
                       ' message ' + e.message)

    def configure_veth(self, mac, ip4, plen, gw):
        '''
        Configure the interface inside container with,
        - Mac-address
        - IP Address
        - Default gateway
        - Link-up
        '''
        ip = IPRoute()
        idx = ip.link_lookup(ifname=self.host_ifname)[0]
        ip.link('set', index=idx, state='up')

        with CniNamespace(self.container_netns, logger):
            ip_ns = IPRoute()
            idx_ns = ip_ns.link_lookup(ifname=self.container_ifname)[0]
            try:
                ip_ns.link('set', index=idx_ns, state='up')
            except NetlinkError as e:
                self.veth_error(e, self.container_ifname,
                                'Error setting link state for veth device')
            try:
                ip_ns.link('set', index=idx_ns, address=mac)
            except NetlinkError as e:
                self.veth_error(e, self.container_ifname,
                                'Error setting mac-address for veth device')

            try:
                ip_ns.addr('add', index=idx_ns, address=ip4,
                           prefixlen=plen)
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    self.veth_error(e, self.container_ifname,
                                    'Error setting ip-address for veth device')
            try:
                ip_ns.route('add', dst='0.0.0.0/0', gateway=gw)
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    self.veth_error(e, self.container_ifname,
                                    'Error adding default route in container')
        return


class Cni():

    def __init__(self, vrouter, params):
        self.vrouter = vrouter
        self.params = params
        self.error_code = 0
        return

    def BuildTapName(self, identifier):
        '''
        Make tap-interface name in host namespace
        '''
        return 'cn-' + str(identifier)

    def BuildCniResponse(self, vr_resp):
        '''
        Build CNI response from VRouter response in json
        '''
        resp = {}
        resp['cniVersion'] = CNI_VERSION
        ip4 = {}
        if vr_resp['ip-address'] != None:
            dns = {}
            dns['nameservers'] = [vr_resp['dns-server']]
            resp['dns'] = dns

            ip4['gateway'] = vr_resp['gateway']
            ip4['ip'] = vr_resp['ip-address'] + '/' + str(vr_resp['plen'])
            route = {}
            route['dst'] = '0.0.0.0/0'
            route['gw'] = vr_resp['gateway']
            routes = [route]
            ip4['routes'] = routes
            resp['ip4'] = ip4
        return resp

    def get_cmd(self):
        resp = self.vrouter.get_cmd(self.params.k8s_params.pod_uuid, None)
        return self.BuildCniResponse(resp)

    def poll_cmd(self):
        resp = self.vrouter.poll_cmd(self.params.k8s_params.pod_uuid, None)
        return self.BuildCniResponse(resp)

    def add_cmd(self):
        '''
        ADD handler for a container
        - Create veth pair
        - Invoke Add handler from VRouter module
        - Update veth pair with configuration got from VRouter
        - stdout VRouter response
        '''
        k8s_params = self.params.k8s_params
        cni_params = self.params
        host_ifname = self.BuildTapName(k8s_params.pod_pid)

        cni_netns = CniNetns(host_ifname, cni_params.container_ifname,
                             cni_params.container_netns)
        cni_netns.create_veth()
        resp = self.vrouter.add_cmd(k8s_params.pod_uuid,
                                    cni_params.container_id,
                                    k8s_params.pod_name,
                                    k8s_params.pod_namespace,
                                    host_ifname,
                                    cni_params.container_ifname)
        cni_netns.configure_veth(resp['mac-address'], resp['ip-address'],
                                 resp['plen'], resp['gateway'])

        return self.BuildCniResponse(resp)

    def delete_cmd(self):
        '''
        DEL handler for a container
        - Delete veth pair
        - Invoke Delete handler from VRouter module
        - stdout VRouter response
        '''
        k8s_params = self.params.k8s_params
        cni_params = self.params
        host_ifname = self.BuildTapName(k8s_params.pod_pid)

        cni_netns = CniNetns(host_ifname, cni_params.container_ifname,
                             cni_params.container_netns)
        cni_netns.delete_veth()
        self.vrouter.delete_cmd(self.params.k8s_params.pod_uuid, None)
        resp = {}
        resp['cniVersion'] = CNI_VERSION
        resp['code'] = 0
        resp['msg'] = 'Delete passed'
        return resp

    def Version(self):
        '''
        Return Version
        '''
        json = {}
        json['cniVersion'] = CNI_VERSION
        json['supportedVersions'] = [CNI_VERSION]
        return json

    def Run(self, vrouter, cni_params):
        '''
        main method for CNI plugin
        '''
        global logger
        logger = Logger.Logger('cni', cni_params.contrail_params.log_file,
                               cni_params.contrail_params.log_level)

        self.params = cni_params
        self.vrouter = vrouter
        ret = True
        code = 0
        if self.params.command.lower() == CNI_CMD_VERSION:
            resp = self.Version()
        elif self.params.command.lower() == CNI_CMD_ADD:
            resp = self.add_cmd()
        elif self.params.command.lower() == CNI_CMD_DELETE:
            resp = self.delete_cmd()
        elif self.params.command.lower() == CNI_CMD_GET:
            resp = self.get_cmd()
        elif self.params.command.lower() == CNI_CMD_POLL:
            resp = self.poll_cmd()
        else:
            raise CniError(CNI_UNSUPPORTED_CMD,
                           'Invalid command ' + self.params.command)

        json_data = json.dumps(resp, indent=4)
        logger.debug('CNI output : ' + json_data)
        print json_data
        return
