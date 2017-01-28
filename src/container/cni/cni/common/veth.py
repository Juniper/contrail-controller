# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
CNI implementation for veth-pair interfaces
"""
import ctypes
import errno
import inspect
import json
import logging
import os
import sys


from pyroute2 import NetlinkError, IPRoute

from interface import Interface as CniInterface
from interface import CniNamespace as CniNamespace
from cni import Error as Error

# Error codes
CNI_ERROR_VETH_ADD = 301


# logger for the file
logger = None


class CniVEthPair(CniInterface, object):
    '''
    Class to manage veth-pair interfaces created for containers.
    Creates a veth-pair and one end of the pair is in host-os and another
    end inside the container.
    '''
    # Number of characters to pick from UUID for ifname
    CNI_UUID_IFNAME_LEN = 11

    def __init__(self, cni, mac):
        self.host_ifname = CniVEthPair._build_tapname(cni.container_uuid)
        self.container_mac = mac
        self.pid = os.getpid()
        CniInterface.__init__(self, cni)
        return

    @staticmethod
    def _build_tapname(uuid):
        '''
        Make tap-interface name in host namespace
        '''
        return 'tap' + str(uuid)[:CniVEthPair.CNI_UUID_IFNAME_LEN]

    def delete_interface(self):
        '''
        Delete the veth-pair.
        Deleting container end of interface will delete both end of veth-pair
        '''
        self.delete_link()
        return

    def create_interface(self):
        '''
        Create veth-pair
        Creates veth-pair in the container-namespace first and then moves
        one end of the pair to host-os namespace
        '''
        # If the host end of veth is already present in host-os, it means
        # interface create was already done. There is nothing to do
        iproute = IPRoute()
        iface = iproute.link_lookup(ifname=self.host_ifname)
        if len(iface) != 0:
            return

        host_ifindex = None
        with CniNamespace(self.cni.container_netns):
            # Create veth pairs if not already created inside namespace
            # One end of pair is named host_ifname and the other end of pair
            # is set a container_ifname
            ns_iproute = IPRoute()
            ns_iface = ns_iproute.link_lookup(ifname=self.cni.container_ifname)
            if len(ns_iface) == 0:
                try:
                    ns_iproute.link_create(ifname=self.cni.container_ifname,
                                        peer=self.host_ifname, kind='veth',
                                        address=self.container_mac)
                except NetlinkError as e:
                    if e.code != errno.EEXIST:
                        raise Error(CNI_ERROR_VETH_ADD,
                                    'Error creating veth device ' +
                                    self.host_ifname + ' code ' +
                                    str(e.code) + ' message ' + e.message)

            # We must move the host part of veth pair to host-namespace
            # Get the interface-index. We will move to host namespace once
            # we exit container-name space and go to host-namespace
            host_ifindex = ns_iproute.link_lookup(ifname=self.host_ifname)[0]

        if host_ifindex is not None:
            ns_iproute.link('set', index=host_ifindex, net_ns_pid=self.pid)

        return

    def configure_interface(self, ip4, plen, gw):
        # Set link-up for interface on host-os
        iproute = IPRoute()
        idx = iproute.link_lookup(ifname=self.host_ifname)[0]
        iproute.link('set', index=idx, state='up')
        super(CniVEthPair, self).configure_interface(ip4, plen, gw)
