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
import logging


from pyroute2 import NetlinkError, IPRoute


from interface import Interface as CniInterface
from interface import CniNamespace as CniNamespace
from cni import Error as Error

CNI_ERROR_GET_PARENT_INTF = 401

CNI_ERROR_ADD_VLAN_INTF = 402
CNI_ERROR_DEL_VLAN_INTF = 403

CNI_ERROR_ADD_MACVLAN = 404
CNI_ERROR_DEL_MACVLAN = 405


# logger for the file
logger = None


class CniMacVlan(CniInterface, object):
    '''
    Class to manage macvlan interfaces for containers.
    This is typically used in nested-k8s scenario where containers are spawned
    inside the container. The VMI for container is modeled as sub-interface in
    this case.

    The class creates a vlan-interface corresponding to the vlan in
    sub-interface and then creates a macvlan interface over it.
    '''
    def __init__(self, cni, mac, host_ifname, tag):
        self.pid = os.getpid()
        self.container_mac = mac
        self.host_ifname = host_ifname
        self.vlan_tag = tag
        self.vlan_ifname = CniMacVlan._make_vlan_intf_name(tag)
        CniInterface.__init__(self, cni)
        return

    @staticmethod
    def _make_vlan_intf_name(tag):
        return 'cn-' + str(tag)

    def delete_interface(self):
        '''
        Delete the interface.
        Deletes both VLAN Tag interface and MACVlan interface
        '''
        # Find the VLAN interface interface from the MACVlan interface
        link = self.get_link()
        if link is None:
            return

        vlan_idx = None
        for i in link[0]['attrs']:
            if (i[0] == 'IFLA_LINK'):
                vlan_idx = i[1]
                break

        if vlan_idx is None:
            raise Error(CNI_ERROR_DEL_VLAN_INTF,
                        'Error finding vlan interface. Interface inside ' +
                        ' container ' + self.cni.container_ifname)

        # Delete the VLAN Tag interface.
        # It will delete the interface inside container also
        try:
            iproute = IPRoute()
            iproute.link('del', index=vlan_idx)
        except NetlinkError as e:
            raise Error(CNI_ERROR_DEL_VLAN_INTF,
                        'Error deleting VLAN interface. Parent interface ' +
                        self.host_ifname + ' vlan-tag ' + self.vlan_tag +
                        ' vlan-ifindex ' + str(vlan_idx) +
                        ' code ' + str(e.code) + ' message ' + e.message)
        return

    def _locate_parent_interface(self, iproute):
        # Ensure the host parent-interface is preset in host network-namespace
        host_if = iproute.link_lookup(ifname=self.host_ifname)
        if len(host_if) == 0:
            raise Error(CNI_ERROR_GET_PARENT_INTF,
                        'Error creating parent interface ' +
                        self.host_ifname + '. Interface not found')

        return host_if[0]

    def _locate_vlan_interface(self, iproute, parent_ifindex):
        # Ensure vlan-interface is created in the host network-namespace
        vlan_if = iproute.link_lookup(ifname=self.vlan_ifname)
        if len(vlan_if) is not 0:
            # vlan-interface already present
            return vlan_if[0]

        try:
            # Create vlan-interface
            iproute.link('add', ifname=self.vlan_ifname, kind='vlan',
                         vlan_id=self.vlan_tag, link=parent_ifindex)
        except NetlinkError as e:
            if e.code != errno.EEXIST:
                raise Error(CNI_ERROR_ADD_VLAN_INTF,
                            'Error creating vlan interface. ' +
                            ' Parent interface ' + self.host_ifname +
                            ' vlan id ' + str(self.vlan_tag) +
                            ' vlan ifname ' + self.vlan_ifname +
                            ' code ' + str(e.code) +
                            ' message ' + e.message)
        vlan_if = iproute.link_lookup(ifname=self.vlan_ifname)
        return vlan_if[0]

    # Ensure the temporary interface is created and moved to
    # container network-namespace
    def _locate_peer_vlan_interface(self, iproute, cn_iproute, vlan_ifindex,
                                    cn_ifname):
        # Check if interface already present in container network-namespace
        cn_intf = cn_iproute.link_lookup(ifname=cn_ifname)
        if len(cn_intf) is not 0:
            return cn_intf[0]

        # Interface not present inside container.
        # Check if it was already created in host network-namespace
        cn_intf = iproute.link_lookup(ifname=cn_ifname)
        if len(cn_intf) == 0:
            # Interface not present in host network-namespace also
            # Create interface in host-os first
            try:
                iproute.link('add', ifname=cn_ifname, kind='macvlan',
                             link=vlan_ifindex, macvlan_mode='vepa',
                             address=self.container_mac)
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    raise Error(CNI_ERROR_ADD_MACVLAN,
                                'Error creating macvlan interface ' +
                                cn_ifname +
                                ' vlan iterface ' + self.vlan_ifname +
                                ' code ' + str(e.code) +
                                ' message ' + e.message)
            cn_intf = iproute.link_lookup(ifname=cn_ifname)

        # Move the temporary interface to container network-namespace
        with CniNamespace(self.cni.container_netns):
            iproute.link('set', index=cn_intf[0], net_ns_pid=self.pid)

        return cn_intf[0]


    def _move_link(self, cn_iproute, cn_intf):
        with CniNamespace(self.cni.container_netns):
            cn_iproute.link('set', index=cn_intf,
                            ifname=self.cni.container_ifname)
        return

    def create_interface(self):
        '''
        Create MACVlan interface
        Creates VLAN interface first based on VLAN tag for sub-interface
        then create macvlan interface above the vlan interface
        '''
        # First check if interface already present inside container
        if self.get_link() is not None:
            return

        if self.vlan_tag is None:
            raise Error(CNI_ERROR_ADD_VLAN_INTF,
                        'Missing vlan-tag for macvlan interface' )

        if self.host_ifname is None:
            raise Error(CNI_ERROR_ADD_VLAN_INTF,
                        'Missing parent-interface for macvlan interface')

        # Open IPRoute socket in both host and container network namespaces
        iproute = IPRoute()
        cn_iproute = None
        with CniNamespace(self.cni.container_netns):
            cn_iproute = IPRoute()

        # Locate the parent interface in host-os network-namespace
        host_ifindex = self._locate_parent_interface(iproute)

        # Locate vlan interface in host-os network-namespace
        vlan_ifindex = self._locate_vlan_interface(iproute, host_ifindex)

        # Creating interface inside container involves following steps,
        # 1. Create a macvlan interface in host network-namespace with a
        #    temporary name
        # 2. Move temporary interface inside container
        # 3. Rename temporary interface to configured name inside container

        # We must also ensure that we recover from any of the failed state
        # in earlier invocation

        # Ensure temporary interface present inside container
        cn_ifname = self.vlan_ifname + '-cn'
        cn_ifindex = self._locate_peer_vlan_interface(iproute, cn_iproute,
                                                      vlan_ifindex, cn_ifname)
        # Move temporary interface to container-ifname
        self._move_link(cn_iproute, cn_ifindex)
        return

    def configure_interface(self, ip4, plen, gw):
        # Set link-up for interface on host-os
        iproute = IPRoute()
        idx = iproute.link_lookup(ifname=self.vlan_ifname)[0]
        iproute.link('set', index=idx, state='up')
        super(CniMacVlan, self).configure_interface(ip4, plen, gw)
