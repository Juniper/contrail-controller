# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
CNI skeletal code for CNI
"""

import errno
import os
import sys
sys.path.insert(0, '/root/kube_cni')
sys.path.insert(0, '/usr/lib/python2.7/dist-packages')
import json
import params.params as Params

# CNI version supported
CNI_VERSION = '0.2.0'

CNI_UNSUPPORTED_CMD = 301
CNI_ERROR_DEL_VETH = 302
CNI_ERROR_ADD_VETH = 303
CNI_ERROR_CONFIG_VETH = 304

# Exception class for CNI related errors
class CniError(RuntimeError):
    def __init__(self, code, msg):
        self.msg = msg
        self.code = code
        return

    def Log(self, logger):
        logger.error('Cni %d : %s', self.code, self.msg)
        return

# Namespace management class for CNI
from nsenter import Namespace
from pyroute2 import NetlinkError
from pyroute2 import IPRoute
class CniNetns():
    def __init__(self, host_ifname, container_ifname, container_pid):
        #FIXME : Cannot always rely on ppid().
        #        What if the parent has changed his namespace temporarily!!!
        self.main_pid = os.getppid()
        self.container_pid = container_pid
        self.host_ifname = host_ifname
        self.container_ifname = container_ifname
        return

    # Delete the veth pair
    def DeleteInterface(self):
        ip = IPRoute()
        iface = ip.link_lookup(ifname=self.host_ifname)
        idx = iface[0]
        if len(iface) == 0:
            return
        try:
            ip.link('del', index = idx)
        except NetlinkError as e:
            raise CniError(CNI_ERROR_DEL_VETH,
                    'Error deleting veth device ' + self.host_ifname +\
                    ' code ' + str(e.code) + ' message ' + e.message)

        return

    # Create veth pair
    # The ifname inside container can be same for multiple containers.
    # Hence, the veth pair cannot be created inside the host namespace.
    # Create veth pair inside container and move one-end to host namespace
    def CreateInterface(self):
        ip = IPRoute()
        iface = ip.link_lookup(ifname = self.host_ifname)
        if len(iface) != 0:
            return

        # Switch to container namespace
        with Namespace(self.container_pid, 'net'):
            ip_ns = IPRoute()
            try:
                # Create veth pairs
                ip_ns.link_create(ifname = self.host_ifname,
                                  peer = self.container_ifname, kind = 'veth')
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    raise CniError(CNI_ERROR_ADD_VETH,
                            'Error creating veth device ' + self.host_ifname +\
                            ' code ' + str(e.code) + ' message ' + e.message)
            # Move one end of veth pair to host network namespace
            idx = ip_ns.link_lookup(ifname = self.host_ifname)[0]
            ip_ns.link('set', index = idx, net_ns_pid = self.main_pid)
        return

    def VEthError(self, e, ifname, message):
        raise CniError(CNI_ERROR_CONFIG_VETH,
                       message + ifname + ' code ' + str(e.code) + \
                       ' message ' + e.message)

    # Configure the interface inside container with,
    # - Mac-address
    # - IP Address
    # - Default gateway
    # - Link-up
    def ConfigureInterface(self, mac, ip4, plen, gw):
        ip = IPRoute()
        idx = ip.link_lookup(ifname = self.host_ifname)[0]
        ip.link('set', index = idx, state = 'up')

        with Namespace(self.container_pid, 'net'):
            ip_ns = IPRoute()
            idx_ns = ip_ns.link_lookup(ifname = self.container_ifname)[0]
            try:
                ip_ns.link('set', index = idx_ns, state = 'up')
            except NetlinkError as e:
                self.VEthError(e, self.container_ifname,
                               'Error setting link state for veth device')
            try:
                ip_ns.link('set', index = idx_ns, address = mac)
            except NetlinkError as e:
                self.VEthError(e, self.container_ifname,
                               'Error setting mac-address for veth device')

            try:
                ip_ns.addr('add', index = idx_ns, address = ip4,
                           prefixlen = plen)
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    self.VEthError(e, self.container_ifname,
                                   'Error setting ip-address for veth device')
            try:
                ip_ns.route('add', dst  = '100.0.0.0/16', gateway = gw)
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    self.VEthError(e, self.container_ifname,
                                   'Error adding default route in container')
        return

class Cni():
    def __init__(self, logger):
        self.logger = logger
        self.params = None
        self.error_code = 0
        return

    # Report CNI error and exit
    def ErrorExit(self, code, msg):
        resp = {}
        resp['cniVersion'] = CNI_VERSION
        resp['code'] = code
        resp['msg'] = msg
        json_data = json.dumps(resp, indent=4)
        print json_data
        sys.exit(code)
        return

    # Make tap-interface name in host namespace
    def BuildTapName(self, identifier):
        return 'cn-' + str(identifier)

    # Build CNI response from VRouter response in json
    def BuildCniResponse(self, vr_resp):
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

    def Get(self):
        resp = self.vrouter.Get(self.params.k8s_params.pod_uuid, None)
        return self.BuildCniResponse(resp)

    def Poll(self):
        resp = self.vrouter.Get(self.params.k8s_params.pod_uuid, None)
        return self.BuildCniResponse(resp)

    # ADD handler for a container
    # - Create veth pair
    # - Invoke Add handler from VRouter module
    # - Update veth pair with configuration got from VRouter
    # - stdout VRouter response
    def Add(self):
        k8s_params = self.params.k8s_params
        cni_params = self.params
        host_ifname = self.BuildTapName(k8s_params.pod_pid)

        cni_netns = CniNetns(host_ifname, cni_params.container_ifname,
                             cni_params.k8s_params.pod_pid)
        cni_netns.CreateInterface()
        resp = self.vrouter.Add(k8s_params.pod_uuid, cni_params.container_id,
                                k8s_params.pod_name, k8s_params.pod_namespace,
                                host_ifname, cni_params.container_ifname)
        cni_netns.ConfigureInterface(resp['mac-address'], resp['ip-address'],
                                     resp['plen'], resp['gateway'])

        return self.BuildCniResponse(resp)

    # DEL handler for a container
    # - Delete veth pair
    # - Invoke Delete handler from VRouter module
    # - stdout VRouter response
    def Delete(self):
        k8s_params = self.params.k8s_params
        cni_params = self.params
        host_ifname = self.BuildTapName(k8s_params.pod_pid)

        cni_netns = CniNetns(host_ifname, cni_params.container_ifname,
                             cni_params.k8s_params.pod_pid)
        cni_netns.DeleteInterface()
        self.vrouter.Delete(self.params.k8s_params.pod_uuid, None)
        resp = {}
        resp['cniVersion'] = CNI_VERSION
        resp['code'] = 0
        resp['msg'] = 'Delete passed'
        return resp

    # Return Version
    def Version(self):
        json = { }
        json['cniVersion'] = CNI_VERSION
        json['supportedVersions'] = [CNI_VERSION]
        return json

    # main method for CNI plugin
    def Run(self, vrouter, cni_params):
        self.params = cni_params
        self.vrouter = vrouter
        ret = True
        code = 0
        if self.params.command.lower() == 'version':
            resp = self.Version()
        if self.params.command.lower() == 'add':
            resp = self.Add()
        elif self.params.command.lower() == 'delete':
            resp = self.Delete()
        elif self.params.command.lower() == 'get':
            resp = self.Get()
        elif self.params.command.lower() == 'poll':
            resp = self.Poll()
        else:
            raise CniError(CNI_UNSUPPORTED_CMD,
                    'Invalid command ' + self.params.command)

        json_data = json.dumps(resp, indent=4)
        print json_data
        return
