# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Module to manage interface inside a container
class Interface is base class. It is further derived to provide implementation
for veth-pair and macvlan interfaces
"""
import ctypes
import errno
import json
import os
import sys
import logging


from pyroute2 import NetlinkError, IPRoute
from cni import Error as Error


CNI_ERROR_NS_ENTER = 201
CNI_ERROR_NS_LEAVE = 202
CNI_ERROR_DEL_NS_INTF = 203
CNI_ERROR_CONFIG_NS_INTF = 204


# logger for the file
logger = None


class CniNamespace(object):
    '''
    Helper class to run processing inside a network-namespace
    The class must be used using 'with' statement as follows,
    with CniNamespace('/proc/<pid>/ns/net'):
        do_something()

    The process changes network-namespace to one given in ns_path. The old
    network-namespace is restored at the end
    '''
    def __init__(self, ns_path):
        self.libc = ctypes.CDLL('libc.so.6', use_errno=True)
        # get current namespace and open fd in current network-namespace
        self.my_path = '/proc/self/ns/net'
        self.my_fd = os.open(self.my_path, os.O_RDONLY)

        # Open fd in network-namespace ns_path
        self.ns_path = ns_path
        self.ns_fd = os.open(self.ns_path, os.O_RDONLY)
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
        logger.debug('Entering namespace <' + self.ns_path + '>')
        # Enter the namespace
        if self.libc.setns(self.ns_fd, 0) == -1:
            e = ctypes.get_errno()
            self.close_files()
            raise Error(CNI_ERROR_NS_ENTER,
                        'Error entering namespace ' + self.ns_path +
                        '. Error ' + str(e) + ' : ' + errno.errorcode[e])
        return

    def __exit__(self, type, value, tb):
        logger.debug('Leaving namespace <' + self.ns_path + '>')
        if self.libc.setns(self.my_fd, 0) == -1:
            e = ctypes.get_errno()
            self.close_files()
            raise Error(CNI_ERROR_NS_LEAVE,
                        'Error leaving namespace ' + self.ns_path +
                        '. Error ' + str(e) + ' : ' + errno.errorcode[e])
        self.close_files()
        return


class Interface():
    '''
    Class for create/delete/configure of interface inside container
    Class is derived further to manage veth-pair and mac-vlan interfaces
    '''
    def __init__(self, cni):
        # configure logger
        global logger
        logger = logging.getLogger('cni-interface')

        self.cni = cni
        return

    def get_link(self):
        '''
        Get link information for the interface inside the container
        '''
        link = None
        with CniNamespace(self.cni.container_netns):
            iproute = IPRoute()
            iface = iproute.link_lookup(ifname=self.cni.container_ifname)
            if len(iface) != 0:
                idx = iface[0]
                link = iproute.link("get", index=idx)
        return link

    def delete_link(self):
        '''
        Delete interface inside the container
        '''
        with CniNamespace(self.cni.container_netns):
            iproute = IPRoute()
            iface = iproute.link_lookup(ifname=self.cni.container_ifname)
            if len(iface) == 0:
                return
            try:
                iproute.link('del', index=iface[0])
            except NetlinkError as e:
                raise Error(CNI_ERROR_DEL_NS_INTF,
                            'Error deleting interface inside container ' +
                            self.cni.container_ifname + ' code ' +
                            str(e.code) + ' message ' + e.message)
        return

    def configure_link(self, ip4_address, plen, gateway):
        '''
        Configure following attributes for interface inside the container
        - Link-up
        - IP Address
        - Default gateway
        '''
        @staticmethod
        def _intf_error(e, ifname, message):
            raise Error(CNI_ERROR_CONFIG_NS_INTF, message + ifname +
                        ' code ' + str(e.code) + ' message ' + e.message)
            return

        with CniNamespace(self.cni.container_netns):
            iproute = IPRoute()
            intf = iproute.link_lookup(ifname=self.cni.container_ifname)
            if len(intf) == 0:
                raise Error(CNI_ERROR_CONFIG_NS_INTF,
                            'Error finding interface ' +
                            self.cni.container_ifname + ' inside container')
            idx_ns = intf[0]
            try:
                iproute.link('set', index=idx_ns, state='up')
            except NetlinkError as e:
                _intf_error(e, self.cni.container_ifname,
                            'Error setting link state for interface ' +
                            'inside container')
            try:
                iproute.addr('add', index=idx_ns, address=ip4_address,
                              prefixlen=plen)
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    _intf_error(e, self.cni.container_ifname,
                                'Error setting ip-address for interface ' +
                                'inside container')
            try:
                iproute.route('add', dst='0.0.0.0/0', gateway=gateway)
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    _intf_error(e, self.cni.container_ifname,
                                'Error adding default route inside container')
        return

    def configure_interface(self, ip4, plen, gw):
        '''
        Configure the interface inside container with,
        - IP Address
        - Default gateway
        - Link-up
        '''
        # Configure interface inside the container
        self.configure_link(ip4, plen, gw)
        return

    def add(self, ip4_address, plen, gateway):
        # Create the interface
        self.create_interface()
        # Configure the interface based on config given in arguments
        self.configure_interface(ip4_address, plen, gateway)
        return

    def delete(self):
        # Delete the interface
        self.delete_interface()
        return
