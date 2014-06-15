# vim: tabstop=4 shiftwidth=4 softtabstop=4

# Copyright (c) 2014 Cloudwatt
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
# @author: Edouard Thuleau, Cloudwatt.

"""
Script to start or destroy a Linux network namespace plug
between two virtual networks. Such that an application can
be executed under the context of a virtualized network.
"""
__docformat__ = "restructuredtext en"

import argparse
import netaddr
import sys
import uuid

from contrail_vrouter_api.vrouter_api import ContrailVRouterApi
from linux import ip_lib


def validate_uuid(val):
    try:
        if str(uuid.UUID(val)) == val:
            return val
    except (TypeError, ValueError, AttributeError):
        raise ValueError('Invalid UUID format')


class NetnsManager(object):
    RT_TABLES_PATH = '/etc/iproute2/rt_tables'
    SNAT_RT_TABLES_NAME = 'inside'
    DEV_NAME_LEN = 14
    NETNS_PREFIX = 'vrouter-'
    LEFT_DEV_PREFIX = 'int-'
    RIGH_DEV_PREFIX = 'gw-'
    TAP_PREFIX = 'veth'

    def __init__(self, vm_uuid, nic_left, nic_right, root_helper='sudo'):
        self.vm_uuid = vm_uuid
        self.namespace = self.NETNS_PREFIX + self.vm_uuid
        self.nic_left = nic_left
        self.nic_right = nic_right
        self.root_helper = root_helper
        self.nic_left['name'] = (self.LEFT_DEV_PREFIX +
                                 self.nic_left['uuid'])[:self.DEV_NAME_LEN]
        self.nic_right['name'] = (self.RIGH_DEV_PREFIX +
                                  self.nic_right['uuid'])[:self.DEV_NAME_LEN]
        self.ip_ns = ip_lib.IPWrapper(root_helper=self.root_helper,
                                      namespace=self.namespace)
        self.vrouter_client = ContrailVRouterApi()

    def _get_tap_name(self, uuid_str):
            return (self.TAP_PREFIX + uuid_str)[:self.DEV_NAME_LEN]

    def is_netns_already_exists(self):
        return self.ip_ns.netns.exists(self.namespace)

    def create(self):
        ip = ip_lib.IPWrapper(self.root_helper)
        ip.ensure_namespace(self.namespace)

        if not self.nic_left or not self.nic_right:
            raise ValueError('Need left and right interfaces to create a '
                             'network namespace')
        for nic in [self.nic_left, self.nic_right]:
            self._create_interfaces(ip, nic)

    def set_snat(self):
        if not self.ip_ns.netns.exists(self.namespace):
            raise ValueError('Need to create the network namespace before set '
                             'up the SNAT')

        if self.SNAT_RT_TABLES_NAME not in open(self.RT_TABLES_PATH).read():
            raise ValueError('Need to set the routing table "%s" into file '
                             '%s' % (self.SNAT_RT_TABLES_NAME,
                                     self.RT_TABLES_PATH))

        self.ip_ns.netns.execute(['sysctl', '-w', 'net.ipv4.ip_forward=1'])
        self.ip_ns.netns.execute(['iptables', '-t', 'nat', '-F'])
        self.ip_ns.netns.execute(['iptables', '-t', 'nat', '-A', 'POSTROUTING',
                                  '-s', '0.0.0.0/0', '-o',
                                  self.nic_right['name'], '-j', 'MASQUERADE'])
        self.ip_ns.netns.execute(['ip', 'route', 'replace', 'default', 'dev',
                                  self.nic_right['name']])
        self.ip_ns.netns.execute(['ip', 'route', 'replace', 'default', 'dev',
                                  self.nic_left['name'], 'table', 'inside'])
        try:
            self.ip_ns.netns.execute(['ip', 'rule', 'del', 'iif',
                                      str(self.nic_right['name']), 'table',
                                      'inside'])
        except RuntimeError:
            pass
        self.ip_ns.netns.execute(['ip', 'rule', 'add', 'iif',
                                  str(self.nic_right['name']), 'table',
                                  'inside'])

    def destroy(self):
        if not self.ip_ns.netns.exists(self.namespace):
            raise ValueError('Namespace %s does not exist' % self.namespace)

        for device in self.ip_ns.get_devices(exclude_loopback=True):
            ip_lib.IPDevice(device.name,
                            self.root_helper,
                            self.namespace).link.delete()

        self.ip_ns.netns.delete(self.namespace)

    def plug_namespace_interface(self):
        if not self.nic_left or not self.nic_right:
            raise ValueError('Need left and right interfaces to plug a '
                             'network namespace onto vrouter')
        for nic in [self.nic_left, self.nic_right]:
            self._add_port_to_agent(nic)

    def unplug_namespace_interface(self):
        if not self.nic_left or not self.nic_right:
            raise ValueError('Need left and right interfaces to unplug a '
                             'network namespace onto vrouter')
        for nic in [self.nic_left, self.nic_right]:
            self._delete_port_to_agent(nic)

    def _create_interfaces(self, ip, nic):
        if ip_lib.device_exists(nic['name'],
                                self.root_helper,
                                namespace=self.namespace):
            ip_lib.IPDevice(nic['name'],
                            self.root_helper,
                            self.namespace).link.delete()

        root_dev, ns_dev = ip.add_veth(self._get_tap_name(nic['uuid']),
                                       nic['name'],
                                       namespace2=self.namespace)
        if nic['mac']:
            ns_dev.link.set_address(str(nic['mac']))

        ns_dev.link.set_up()
        root_dev.link.set_up()

        if nic['ip']:
            ip_addr = nic['ip']
            ns_dev.addr.flush()
            ns_dev.addr.add(4, str(ip_addr), str(ip_addr.broadcast))
        else:
            #TODO(ethuleau): start DHCP client
            raise NotImplementedError

        # disable reverse path filtering
        self.ip_ns.netns.execute(['sysctl', '-w',
                                  'net.ipv4.conf.%s.rp_filter=2' % nic['name']]
                                 )

    def _add_port_to_agent(self, nic):
        kwargs = {}
        kwargs['ip_address'] = str(nic['ip'].ip)
        self.vrouter_client.add_port(self.vm_uuid, nic['uuid'],
                                     self._get_tap_name(nic['uuid']),
                                     str(nic['mac']), **kwargs)

    def _delete_port_to_agent(self, nic):
        try:
            self.vrouter_client.delete_port(nic['uuid'])
        except KeyError:
            # That script does not maintain vrouter port list with the vrouter
            # API library. So when it deletes a port the vrouter API library
            # raises a KeyError exception
            pass


class VRouterNetns(object):
    """Create or destroy a Linux network namespace plug
    between two virtual networks.
    """

    SOURCE_NAT = 'source-nat'
    LOAD_BALANCER = 'load-balancer'
    SERVICE_TYPES = [SOURCE_NAT, LOAD_BALANCER]

    def __init__(self, args_str=None):
        self.args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

    def _parse_args(self, args_str):
        """Return an argparse.ArgumentParser for me"""
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--root_helper",
                                 help="Helper to execute root commands. "
                                 "Default: 'sudo'", default="sudo")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())
        # Override with CLI options
        # Don't surpress add_help here so it will handle -h
        parser = argparse.ArgumentParser(
            # Inherit options from config_parser
            parents=[conf_parser],
            # print script description with -h/--help
            description=__doc__,
            # Don't mess with format of description
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        subparsers = parser.add_subparsers()

        create_parser = subparsers.add_parser('create')
        create_parser.add_argument(
            "service_type",
            choices=self.SERVICE_TYPES,
            help="Service type to run into the namespace")
        create_parser.add_argument(
            "vm_id",
            help="Virtual machine UUID")
        create_parser.add_argument(
            "vmi_left_id",
            help="Left virtual machine interface UUID")
        create_parser.add_argument(
            "vmi_right_id",
            help="Right virtual machine interface UUID")
        create_parser.add_argument(
            "--vmi-left-mac",
            default=None,
            help=("Left virtual machine interface MAC. Default: automatically "
                  "generated by the system"))
        create_parser.add_argument(
            "--vmi-left-ip",
            default=None,
            help=("Left virtual machine interface IPv4 and mask "
                  "(ie: a.a.a.a/bb). Default mask to /32"))
        create_parser.add_argument(
            "--vmi-right-mac",
            default=None,
            help=("Right virtual machine interface MAC. Default: "
                  "automatically generated by the system"))
        create_parser.add_argument(
            "--vmi-right-ip",
            default=None,
            help=("Right virtual machine interface IPv4 and mask "
                  "(ie: a.a.a.a/bb). Default mask to /32"))
        create_parser.add_argument(
            "--update",
            action="store_true",
            default=False,
            help=("Update a created namespace (do nothing for the moment)"))
        create_parser.set_defaults(func=self.create)

        destroy_parser = subparsers.add_parser('destroy')
        destroy_parser.add_argument(
            "service_type",
            choices=self.SERVICE_TYPES,
            help="Service type to run into the namespace")
        destroy_parser.add_argument(
            "vm_id",
            help="Virtual machine UUID")
        destroy_parser.add_argument(
            "vmi_left_id",
            help="Left virtual machine interface UUID")
        destroy_parser.add_argument(
            "vmi_right_id",
            help="Right virtual machine interface UUID")
        destroy_parser.set_defaults(func=self.destroy)

        self.args = parser.parse_args(remaining_argv)

    def create(self):
        netns_name = validate_uuid(self.args.vm_id)

        nic_left = {}
        nic_left['uuid'] = validate_uuid(self.args.vmi_left_id)
        if self.args.vmi_left_mac:
            nic_left['mac'] = netaddr.EUI(self.args.vmi_left_mac,
                                          dialect=netaddr.mac_unix)
        else:
            nic_left['mac'] = None
        if self.args.vmi_left_ip:
            nic_left['ip'] = netaddr.IPNetwork(self.args.vmi_left_ip)
        else:
            nic_left['ip'] = None

        nic_right = {}
        nic_right['uuid'] = validate_uuid(self.args.vmi_right_id)
        if self.args.vmi_right_mac:
            nic_right['mac'] = netaddr.EUI(self.args.vmi_right_mac,
                                           dialect=netaddr.mac_unix)
        else:
            nic_right['mac'] = None
        if self.args.vmi_right_ip:
            nic_right['ip'] = netaddr.IPNetwork(self.args.vmi_right_ip)
        else:
            nic_right['ip'] = None

        netns_mgr = NetnsManager(netns_name, nic_left, nic_right,
                                 root_helper=self.args.root_helper)

        if netns_mgr.is_netns_already_exists():
            # If the netns already exists, destroy it to be sure to set it
            # with new parameters like another external network
            netns_mgr.unplug_namespace_interface()
            netns_mgr.destroy()
        netns_mgr.create()

        if self.args.service_type == self.SOURCE_NAT:
            netns_mgr.set_snat()
        elif self.args.service_type == self.LOAD_BALANCER:
            raise NotImplementedError('I need to be implemented!')
        else:
            msg = ('The %s service type is not supported' %
                   self.args.service_type)
            raise NotImplementedError(msg)

        netns_mgr.plug_namespace_interface()

    def destroy(self):
        netns_name = validate_uuid(self.args.vm_id)
        nic_left = {'uuid': validate_uuid(self.args.vmi_left_id)}
        nic_right = {'uuid': validate_uuid(self.args.vmi_right_id)}

        netns_mgr = NetnsManager(netns_name, nic_left, nic_right,
                                 root_helper=self.args.root_helper)

        netns_mgr.unplug_namespace_interface()

        if self.args.service_type == self.SOURCE_NAT:
            netns_mgr.destroy()
        elif self.args.service_type == self.LOAD_BALANCER:
            raise NotImplementedError('I need to be implemented!')
        else:
            msg = ('The %s service type is not supported' %
                   self.args.service_type)
            raise NotImplementedError(msg)


def main(args_str=None):
    vrouter_netns = VRouterNetns(args_str)
    vrouter_netns.args.func()
