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
from __future__ import print_function
__docformat__ = "restructuredtext en"

import argparse
import netaddr
import sys
import uuid
import subprocess
import requests
import json

from linux import ip_lib
import haproxy_process


def validate_uuid(val):
    try:
        if str(uuid.UUID(val)) == val:
            return val
    except (TypeError, ValueError, AttributeError):
        raise ValueError('Invalid UUID format')


class NetnsManager(object):
    SNAT_RT_TABLES_ID = 42
    DEV_NAME_LEN = 14
    NETNS_PREFIX = 'vrouter-'
    LEFT_DEV_PREFIX = 'int-'
    RIGH_DEV_PREFIX = 'gw-'
    TAP_PREFIX = 'veth'
    PORT_TYPE = 'NameSpacePort'
    LBAAS_PROCESS = 'haproxy'
    BASE_URL = "http://localhost:9091/port"
    HEADERS = {'content-type': 'application/json'}

    def __init__(self, vm_uuid, nic_left, nic_right, other_nics=None,
                 root_helper='sudo', cfg_file=None, update=False,
                 pool_id=None, gw_ip=None, namespace_name=None,
                 keystone_auth_cfg_file=None, loadbalancer_id=None):
        self.vm_uuid = vm_uuid
        if namespace_name is None:
            self.namespace = self.NETNS_PREFIX + self.vm_uuid
        else:
            self.namespace = namespace_name
        if pool_id:
            self.namespace = self.namespace + ":" + pool_id
        elif loadbalancer_id:
            self.namespace = self.namespace + ":" + loadbalancer_id
        self.nic_left = nic_left
        self.nic_right = nic_right
        self.root_helper = root_helper
        self.nics = other_nics or []
        if self.nic_left:
            self.nic_left['name'] = (self.LEFT_DEV_PREFIX +
                                 self.nic_left['uuid'])[:self.DEV_NAME_LEN]
            self.nics.append(self.nic_left)
        if self.nic_right:
            self.nic_right['name'] = (self.RIGH_DEV_PREFIX +
                                      self.nic_right['uuid'])[:self.DEV_NAME_LEN]
            self.nics.append(self.nic_right)
        self.ip_ns = ip_lib.IPWrapper(root_helper=self.root_helper,
                                      namespace=self.namespace)
        self.cfg_file = cfg_file
        self.update = update
        self.gw_ip = gw_ip
        self.keystone_auth_cfg_file = keystone_auth_cfg_file

    def _get_tap_name(self, uuid_str):
            return (self.TAP_PREFIX + uuid_str)[:self.DEV_NAME_LEN]

    def is_netns_already_exists(self):
        return self.ip_ns.netns.exists(self.namespace)

    def create(self):
        ip = ip_lib.IPWrapper(self.root_helper)
        ip.ensure_namespace(self.namespace)
        for nic in self.nics:
            self._create_interfaces(ip, nic)

    def set_snat(self):
        if not self.ip_ns.netns.exists(self.namespace):
            self.create()

        self.ip_ns.netns.execute(['sysctl', '-w', 'net.ipv4.ip_forward=1'])
        self.ip_ns.netns.execute(['iptables', '-t', 'nat', '-F'])
        self.ip_ns.netns.execute(['iptables', '-t', 'nat', '-A', 'POSTROUTING',
                                  '-s', '0.0.0.0/0', '-o',
                                  self.nic_right['name'], '-j', 'MASQUERADE'])
        self.ip_ns.netns.execute(['ip', 'route', 'replace', 'default', 'dev',
                                  self.nic_right['name']])
        self.ip_ns.netns.execute(['ip', 'route', 'replace', 'default', 'dev',
                                  self.nic_left['name'], 'table',
                                  self.SNAT_RT_TABLES_ID])
        try:
            self.ip_ns.netns.execute(['ip', 'rule', 'del', 'iif',
                                      str(self.nic_right['name']), 'table',
                                      self.SNAT_RT_TABLES_ID])
        except RuntimeError:
            pass
        self.ip_ns.netns.execute(['ip', 'rule', 'add', 'iif',
                                  str(self.nic_right['name']), 'table',
                                  self.SNAT_RT_TABLES_ID])

        self.ip_ns.netns.execute(['ip', 'route', 'del', 'default', 'table',
                                 self.SNAT_RT_TABLES_ID])

        self.ip_ns.netns.execute(['ip', 'route', 'add', 'default', 'table',
                                 self.SNAT_RT_TABLES_ID, 'via',  self.gw_ip,
                                 'dev', str(self.nic_left['name'])])

    def set_lbaas(self):
        if not self.ip_ns.netns.exists(self.namespace):
            self.create()

        haproxy_process.start_update_haproxy(self.cfg_file, self.namespace, True,
                                             self.keystone_auth_cfg_file)

        try:
            self.ip_ns.netns.execute(['route', 'add', 'default', 'gw', self.gw_ip])
        except RuntimeError:
            pass

    def release_lbaas(self):
        if not self.ip_ns.netns.exists(self.namespace):
            raise ValueError('Need to create the network namespace before '
                             'relasing lbaas')

        haproxy_process.stop_haproxy(self.cfg_file, True)

        try:
            self.ip_ns.netns.execute(['route', 'del', 'default'])
        except RuntimeError:
            pass

    def destroy(self):
        if not self.ip_ns.netns.exists(self.namespace):
            raise ValueError('Namespace %s does not exist' % self.namespace)

        for device in self.ip_ns.get_devices(exclude_loopback=True):
            ip_lib.IPDevice(device.name,
                            self.root_helper,
                            self.namespace).link.delete()

        self.ip_ns.netns.delete(self.namespace)

    def plug_namespace_interface(self):
        for nic in self.nics:
            self._add_port_to_agent(nic,
                                    display_name='NetNS-%s-%s-interface'
                                                 % (self.vm_uuid, nic['name']))

    def unplug_namespace_interface(self):
        for nic in self.nics:
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
            ip = nic['ip']
            ns_dev.addr.flush()
            ns_dev.addr.add(ip.version, str(ip), str(ip.broadcast))
        else:
            #TODO(ethuleau): start DHCP client
            raise NotImplementedError

        # disable reverse path filtering
        self.ip_ns.netns.execute(['sysctl', '-w',
                                  'net.ipv4.conf.%s.rp_filter=2' % nic['name']]
                                 )

    def _request_to_agent(self, url, method, data):
        method = getattr(requests, method)
        resp = method(url, data=data, headers=self.HEADERS)
        if resp.status_code != requests.codes.ok:
            error_str = resp.text
            try:
                err = json.loads(resp.text)
                error_str = err['error']
            except Exception:
                pass
            raise ValueError(error_str)

    def _add_port_to_agent(self, nic, display_name=None):
        if self.PORT_TYPE == "NovaVMPort":
            port_type_value = 0
        elif self.PORT_TYPE == "NameSpacePort":
            port_type_value = 1
        payload = {"ip-address": str(nic['ip'].ip), "tx-vlan-id": -1,
                   "display-name": display_name, "id": nic['uuid'],
                   "instance-id": self.vm_uuid, "ip6-address": '',
                   "rx-vlan-id": -1,
                   "system-name": self._get_tap_name(nic['uuid']),
                   "vn-id": '', "vm-project-id": '',
                   "type": port_type_value, "mac-address": str(nic['mac'])}
        json_dump = json.dumps(payload)
        self._request_to_agent(self.BASE_URL, 'post', json_dump)

    def _delete_port_to_agent(self, nic):
        url = self.BASE_URL + "/" + nic['uuid']
        self._request_to_agent(url, 'delete', None)


class VRouterNetns(object):
    """Create or destroy a Linux network namespace plug
    between two virtual networks.
    """

    SOURCE_NAT = 'source-nat'
    LOAD_BALANCER = 'loadbalancer'
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
        create_parser.add_argument(
            "--cfg-file",
            default=None,
            help=("Config file for lbaas"))
        create_parser.add_argument(
            "--gw-ip",
            default=None,
            help=("Gateway IP for Virtual Network"))
        create_parser.add_argument(
            "--pool-id",
            default=None,
            help=("Loadbalancer Pool"))
        create_parser.add_argument(
            "--loadbalancer-id",
            default=None,
            help=("Loadbalancer"))
        create_parser.add_argument(
            "--keystone-auth-cfg-file",
            default=None,
            help=("Keystone auth config file for lbaas"))
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
        destroy_parser.add_argument(
            "--cfg-file",
            default=None,
            help=("config file for lbaas"))
        destroy_parser.add_argument(
            "--pool-id",
            default=None,
            help=("Loadbalancer Pool"))
        destroy_parser.add_argument(
            "--loadbalancer-id",
            default=None,
            help=("Loadbalancer"))
        destroy_parser.set_defaults(func=self.destroy)

        self.args = parser.parse_args(remaining_argv)

    def create(self):
        netns_name = validate_uuid(self.args.vm_id)

        nic_left = {}
        if uuid.UUID(self.args.vmi_left_id).int:
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
        if uuid.UUID(self.args.vmi_right_id).int:
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
                             root_helper=self.args.root_helper,
                             cfg_file=self.args.cfg_file,
                             update=self.args.update, gw_ip=self.args.gw_ip,
                             pool_id=self.args.pool_id,
                             loadbalancer_id=self.args.loadbalancer_id,
                             keystone_auth_cfg_file=self.args.keystone_auth_cfg_file)

        if (self.args.update is False):
            if netns_mgr.is_netns_already_exists():
                # If the netns already exists, destroy it to be sure to set it
                # with new parameters like another external network
                if self.args.service_type == self.LOAD_BALANCER:
                    netns_mgr.release_lbaas()
                netns_mgr.unplug_namespace_interface()
                netns_mgr.destroy()
            netns_mgr.create()

        if self.args.service_type == self.SOURCE_NAT:
            netns_mgr.set_snat()
        elif self.args.service_type == self.LOAD_BALANCER:
            netns_mgr.set_lbaas()
        else:
            msg = ('The %s service type is not supported' %
                   self.args.service_type)
            raise NotImplementedError(msg)

        netns_mgr.plug_namespace_interface()

    def destroy(self):
        netns_name = validate_uuid(self.args.vm_id)
        nic_left = {}
        if uuid.UUID(self.args.vmi_left_id).int:
            nic_left = {'uuid': validate_uuid(self.args.vmi_left_id)}
        nic_right = {}
        if uuid.UUID(self.args.vmi_right_id).int:
            nic_right = {'uuid': validate_uuid(self.args.vmi_right_id)}

        netns_mgr = NetnsManager(netns_name, nic_left, nic_right,
                                 root_helper=self.args.root_helper,
                                 cfg_file=self.args.cfg_file, gw_ip=None,
                                 pool_id=self.args.pool_id,
                                 loadbalancer_id=self.args.loadbalancer_id)

        netns_mgr.unplug_namespace_interface()
        if self.args.service_type == self.SOURCE_NAT:
            netns_mgr.destroy()
        elif self.args.service_type == self.LOAD_BALANCER:
            netns_mgr.release_lbaas()
            netns_mgr.destroy()
        else:
            msg = ('The %s service type is not supported' %
                   self.args.service_type)
            raise NotImplementedError(msg)

def _check_output(cmd, flag=True):
    proc = subprocess.Popen(cmd, shell=flag, stdout=subprocess.PIPE)
    data, err = proc.communicate()
    retcode = proc.poll()
    if retcode:
       raise subprocess.CalledProcessError(retcode, cmd)
    return data

def main(args_str=None):
    vrouter_netns = VRouterNetns(args_str)
    vrouter_netns.args.func()
