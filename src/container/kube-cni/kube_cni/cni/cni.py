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


from pyroute2 import NetlinkError, IPRoute


# set parent directory in sys.path
cfile = os.path.abspath(inspect.getfile(inspect.currentframe()))  # nopep8
sys.path.append(os.path.dirname(os.path.dirname(cfile)))  # nopep8
from common import logger as Logger
from params import params as Params

# CNI commands
CNI_CMD_VERSION = 'version'
CNI_CMD_GET = 'get'
CNI_CMD_POLL = 'poll'
CNI_CMD_ADD = 'add'
CNI_CMD_DELETE = 'delete'
CNI_CMD_DEL = 'del'

# CNI version supported
CNI_VERSION = '0.2.0'

# Error codes
CNI_UNSUPPORTED_CMD = 301
CNI_ERROR_NS_ENTER = 302
CNI_ERROR_NS_LEAVE = 303


CNI_ERROR_CONFIG_NS_INTF = 304
CNI_ERROR_DEL_NS_INTF = 305
CNI_ERROR_ADD_NS_INTF = 306

CNI_ERROR_DEL_VETH = 307
CNI_ERROR_ADD_VETH = 308
CNI_ERROR_CONFIG_VETH = 309

CNI_ERROR_DEL_VLAN_INTF = 310
CNI_ERROR_ADD_VLAN_INTF = 311
CNI_ERROR_CONFIG_VLAN_INTF = 312

CNI_ERROR_DEL_MACVLAN = 313
CNI_ERROR_ADD_MACVLAN = 314
CNI_ERROR_CONFIG_MACVLAN = 315


# logger for the file
logger = None


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
    Exception class to report CNI processing errors
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


class CniNsInterface():
    '''
    Class to manage interface inside the container
    '''
    def __init__(self, container_ifname, mac, container_netns, container_pid):
        self.container_pid = container_pid
        self.container_ifname = container_ifname
        self.mac = mac
        self.container_netns = container_netns
        return

    def get_link(self):
        '''
        Get link information for the interface inside the container
        '''
        link = None
        with CniNamespace(self.container_netns, logger):
            iproute = IPRoute()
            iface = iproute.link_lookup(ifname=self.container_ifname)
            if len(iface) != 0:
                idx = iface[0]
                link = iproute.link("get", index=idx)
        return link

    def delete_link(self):
        '''
        Delete interface inside the container
        '''
        with CniNamespace(self.container_netns, logger):
            iproute = IPRoute()
            iface = iproute.link_lookup(ifname=self.container_ifname)
            if len(iface) == 0:
                return
            try:
                iproute.link('del', index=iface[0])
            except NetlinkError as e:
                raise CniError(CNI_ERROR_DEL_NS_INTF,
                               'Error deleting interface inside container ' +
                               self.container_ifname +
                               ' code ' + str(e.code) + ' message ' + e.message)
        return

    def move_link(self, ifname):
        '''
        Move interface inside a container and rename to self.container_ifname
        '''
        # Get index of interface being configured
        iproute = IPRoute()
        intf = iproute.link_lookup(ifname=ifname)
        if len(intf) == 0:
            return
        idx = intf[0]

        # Move interface inside container.
        # If the interface rename failed, the interface with temporary name
        # can be present inside interface
        iproute.link('set', index=idx, net_ns_pid=self.container_pid)

        # Rename the interface
        with CniNamespace(self.container_netns, logger):
            ip_ns = IPRoute()
            if ifname != self.container_ifname:
                ip_ns.link("set", index=idx, address=self.mac,
                           ifname=self.container_ifname)
        return

    def configure_link(self, ip4, plen, gw):
        '''
        Configure following attributes for interface inside the container
        - Link-up
        - IP Address
        - Default gateway
        '''

        def intf_error(self, e, ifname, message):
            raise CniError(CNI_ERROR_CONFIG_NS_INTF, message + ifname +
                           ' code ' + str(e.code) + ' message ' + e.message)

        with CniNamespace(self.container_netns, logger):
            iproute = IPRoute()
            intf = iproute.link_lookup(ifname=self.container_ifname)
            if len(intf) == 0:
                self.intf_error(e, self.container_ifname,
                                'Error finding interface inside container')
                return
            idx_ns = intf[0]
            try:
                iproute.link('set', index=idx_ns, state='up')
            except NetlinkError as e:
                self.intf_error(e, self.container_ifname,
                                'Error setting link state for interface ' +
                                'inside container')
            try:
                iproute.addr('add', index=idx_ns, address=ip4, prefixlen=plen)
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    self.intf_error(e, self.container_ifname,
                                    'Error setting ip-address for interface ' +
                                    'inside container')
            try:
                iproute.route('add', dst='0.0.0.0/0', gateway=gw)
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    self.intf_error(e, self.container_ifname,
                                    'Error adding default route inside ' +
                                    'container')
        return


class CniVEthPair(CniNsInterface):
    '''
    Class to manage veth-pair interfaces created for containers.
    Creates a veth-pair and one end of the pair is in host-os and another
    end inside the container.
    '''
    def __init__(self, host_ifname, container_ifname, mac, container_netns,
                 container_pid):
        self.host_ifname = host_ifname
        CniNsInterface.__init__(self, container_ifname, mac, container_netns,
                                container_pid)
        return

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
        Creates veth-pair in the host-os first and then one end of the pair is
        moved inside container.

        Name of interface inside container can be same for multiple containers.
        So, the veth-pair cannot be created with name of interface inside
        cotnainer. The veth-pair is created with a temporary name and will be
        renamed later
        '''

        # Check if interface already created
        iproute = IPRoute()
        iface = iproute.link_lookup(ifname=self.host_ifname)
        if len(iface) != 0:
            return

        # Create veth pairs. One end of pair is named host_ifname and the
        # other end of pair is set a temporary name. It will be overridden
        # once interface is moved inside container
        try:
            cn_name = self.host_ifname + '-ns'
            iproute.link_create(ifname=self.host_ifname, peer=cn_name,
                                kind='veth')
        except NetlinkError as e:
            if e.code != errno.EEXIST:
                raise CniError(CNI_ERROR_ADD_VETH,
                               'Error creating veth device ' +
                               self.host_ifname + ' code ' + str(e.code) +
                               ' message ' + e.message)
        # Move one end of pair inside container
        self.move_link(cn_name)
        return

    def configure_interface(self, ip4, plen, gw):
        '''
        Configure the interface inside container with,
        - Link-up
        - IP Address
        - Default gateway

        Set link-up for interface inside host-os
        '''
        # Configure interface inside container
        self.configure_link(ip4, plen, gw)

        # Set link-up for interface on host-os
        iproute = IPRoute()
        idx = iproute.link_lookup(ifname=self.host_ifname)[0]
        iproute.link('set', index=idx, state='up')
        return


class CniMacVlan(CniNsInterface):
    '''
    Class to manage macvlan interfaces for containers.
    This is typically used in nested-k8s scenario where containers are spawned
    inside the container. The VMI for container is modeled as sub-interface in
    this case.

    The class creates a vlan-interface corresponding to the vlan in
    sub-interface and then creates a macvlan interface over it.
    '''
    @staticmethod
    def make_vlan_intf_name(tag):
        return 'cn-' + str(tag)

    def __init__(self, host_ifname, container_ifname, mac, tag,
                 container_netns, container_pid):
        self.host_ifname = host_ifname
        self.vlan_tag = tag
        self.vlan_ifname = CniMacVlan.make_vlan_intf_name(tag)
        CniNsInterface.__init__(self, container_ifname, mac, container_netns,
                                container_pid)
        return

    def delete_interface(self):
        '''
        Delete the interface.
        Deletes both VLAN Tag interface and MACVlan interface
        '''
        # Find the VLAN interface interface from the MACVlan interface
        link = self.get_link()
        if link is None:
            raise CniError(CNI_ERROR_DEL_NS_INTF,
                           'Error finding interface inside container ' +
                           self.container_ifname)
        vlan_idx = None
        for i in link[0]['attrs']:
            if (i[0] == 'IFLA_LINK'):
                vlan_idx = i[1]
                break

        if vlan_idx is None:
            raise CniError(CNI_ERROR_DEL_VLAN_INTF,
                           'Error finding vlan interface. Interface inside ' +
                           ' container ' + self.container_ifname)

        # Delete the VLAN Tag interface.
        # It will delete the interface inside container also
        try:
            iproute = IPRoute()
            iproute.link('del', index=vlan_idx)
        except NetlinkError as e:
            raise CniError(CNI_ERROR_DEL_VLAN_INTF,
                           'Error deleting VLAN interface. host-ifname ' +
                           self.host_ifname + ' vlan-tag ' + self.vlan_tag +
                           ' vlan-ifname ' + self.vlan_ifname +
                           ' code ' + str(e.code) + ' message ' + e.message)
        return

    def create_interface(self):
        '''
        Create MACVlan interface
        Creates VLAN interface first based on VLAN tag for sub-interface
        then create macvlan interface above the vlan interface
        '''

        iproute = IPRoute()
        # Ensure the host interface is present
        host_if = iproute.link_lookup(ifname=self.host_ifname)
        if len(host_if) == 0:
            raise CniError(CNI_ERROR_ADD_VLAN_INTF,
                           'Error creating vlan interface' + ' host interface' +
                           self.host_ifname + ' not found')
            return

        # Create vlan interface if not present
        vlan_if = iproute.link_lookup(ifname=self.vlan_ifname)
        if len(vlan_if) == 0:
            try:
                iproute.link("add", ifname=self.vlan_ifname, kind='vlan',
                             vlan_id=self.vlan_tag, link=host_if[0])
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    raise CniError(CNI_ERROR_ADD_VETH,
                                   'Error creating vlan interface. ' +
                                   'Host interface ' + self.host_ifname +
                                   'vlan id ' + str(self.vlan_tag) +
                                   'vlan ifname ' + self.vlan_ifname +
                                   ' code ' + str(e.code) +
                                   ' message ' + e.message)
            vlan_if = iproute.link_lookup(ifname=self.vlan_ifname)

        # Create MACVlan interface if not present
        cn_ifname = self.vlan_ifname + '-ns'
        cn_if = iproute.link_lookup(ifname=cn_ifname)
        if len(cn_if) == 0:
            try:
                iproute.link("add", ifname=cn_ifname, kind='macvlan',
                             link=vlan_if[0], macvlan_mode="vepa")
            except NetlinkError as e:
                if e.code != errno.EEXIST:
                    raise CniError(CNI_ERROR_ADD_VETH,
                                   'Error creating macvlan interface ' +
                                   ' vlan iterface ' + self.vlan_ifname +
                                   ' macvlan interface ' + cn_ifname +
                                   ' code ' + str(e.code) +
                                   ' message ' + e.message)
            cn_if = iproute.link_lookup(ifname=self.vlan_ifname)

        # Move one end of pair inside container
        self.move_link(cn_ifname)
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
        # Set link-up for vlan interface on host-os
        iproute = IPRoute()
        idx = iproute.link_lookup(ifname=self.vlan_ifname)[0]
        iproute.link('set', index=idx, state='up')
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
        - Pre-fetch interface configuration from VRouter.
          - Gets MAC address for the interface
          - In case of sub-interface, gets VLAN-Tag for the interface
        - Create interface based on the "mode"
        - Invoke Add handler from VRouter module
        - Update interface with configuration got from VRouter
          - Configures IP address
          - Configures routes
          - Bring-up the interface
        - stdout CNI response
        '''
        cni_params = self.params
        k8s_params = cni_params.k8s_params
        contrail_params = cni_params.contrail_params

        # Pre-fetch initial configuration for the interface
        # This will give MAC address for the interface and in case of
        # VMI sub-interface, we will also get the vlan-tag
        cfg = self.vrouter.poll_cfg_cmd(self.params.k8s_params.pod_uuid, None)

        # Create the interface object
        intf = None
        if contrail_params.mode == Params.CONTRAIL_CNI_MODE_CONTRAIL_K8S:
            host_ifname = contrail_params.parent_interface
            intf = CniMacVlan(host_ifname, cni_params.container_ifname,
                              cfg['mac-address'], cfg['vlan-id'],
                              cni_params.container_netns, k8s_params.pod_pid)
        else:
            host_ifname = self.BuildTapName(k8s_params.pod_pid)
            intf = CniVEthPair(host_ifname, cni_params.container_ifname,
                               cfg['mac-address'], cni_params.container_netns,
                               k8s_params.pod_pid)
        # Create the interface both in host-os and inside container
        intf.create_interface()

        # Inform vrouter about interface-add. The interface inside container
        # must be created by this time
        resp = self.vrouter.add_cmd(k8s_params.pod_uuid,
                                    cni_params.container_id,
                                    k8s_params.pod_name,
                                    k8s_params.pod_namespace,
                                    host_ifname,
                                    cni_params.container_ifname)
        # Configure the interface based on config received above
        intf.configure_interface(resp['ip-address'], resp['plen'],
                                 resp['gateway'])

        # Build CNI response and print on stdout
        return self.BuildCniResponse(resp)

    def delete_cmd(self):
        '''
        DEL handler for a container
        - Delete veth pair
        - Invoke Delete handler from VRouter module
        - stdout VRouter response
        '''
        cni_params = self.params
        k8s_params = cni_params.k8s_params
        contrail_params = cni_params.contrail_params

        intf = None
        # create local interface structure
        if contrail_params.mode == Params.CONTRAIL_CNI_MODE_CONTRAIL_K8S:
            host_ifname = contrail_params.parent_interface
            intf = CniMacVlan(host_ifname, cni_params.container_ifname,
                              None, None, cni_params.container_netns,
                              k8s_params.pod_pid)
        else:
            host_ifname = self.BuildTapName(k8s_params.pod_pid)
            intf = CniVEthPair(host_ifname, cni_params.container_ifname, None,
                               cni_params.container_netns, k8s_params.pod_pid)
        # Delete the interface
        intf.delete_interface()

        # Inform VRouter about interface delete
        self.vrouter.delete_cmd(self.params.k8s_params.pod_uuid, None)

        # Return response
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
        if self.params.command.lower() == CNI_CMD_VERSION:
            resp = self.Version()
        elif self.params.command.lower() == CNI_CMD_ADD:
            resp = self.add_cmd()
        elif (self.params.command.lower() == CNI_CMD_DELETE or
              self.params.command.lower() == CNI_CMD_DEL):
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
