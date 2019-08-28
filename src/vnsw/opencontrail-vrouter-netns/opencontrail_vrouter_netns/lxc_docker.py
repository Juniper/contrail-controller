"""
This script associates a network namespace with a docker instance.
"""
from __future__ import print_function
from __future__ import absolute_import

import argparse
import socket
import subprocess
import sys

from .instance_provisioner import Provisioner
from .lxc_manager import LxcManager
from .vrouter_control import interface_register, interface_unregister

def build_network_name(project_name, network_name):
    if network_name.find(':') >= 0:
        return network_name
    return "%s:%s" % (project_name, network_name)

def main():
    parser = argparse.ArgumentParser()
    defaults = {
        'api-server': '127.0.0.1',
        'api-port': 8082,
        'project': 'default-domain:default-project',
        'network': 'default-network',
    }
    parser.set_defaults(**defaults)
    parser.add_argument("-s", "--api-server", help="API server address")
    parser.add_argument("-p", "--api-port", type=int, help="API server port")
    parser.add_argument("-n", "--network", help="Primary network")
    parser.add_argument("--project", help="Network project")
    parser.add_argument("--start", action='store_true', help="Create namespace")
    parser.add_argument("--stop", action='store_true', help="Delete namespace")
    parser.add_argument("container_id", metavar='container-id', help="Container ID")

    args = parser.parse_args(sys.argv[1:])
    if not args.start and not args.stop:
        print("Please specify --start or --stop action")
        sys.exit(1)

    manager = LxcManager()
    provisioner = Provisioner(api_server=args.api_server, api_port=args.api_port)
    instance_name = '%s-%s' % (socket.getfqdn(), args.container_id)
    pid_str = subprocess.check_output(
        'docker inspect -f \'{{.State.Pid}}\' %s' % args.container_id, shell=True)
    pid = int(pid_str)

    subprocess.check_output(
        'ln -sf /proc/%d/ns/net /var/run/netns/%s' % (pid, args.container_id), shell=True)

    if args.start:
        vm = provisioner.virtual_machine_locate(instance_name)
        network = build_network_name(args.project, args.network)
        vmi = provisioner.vmi_locate(vm, network, 'veth0')
        ifname = manager.create_interface(args.container_id, 'veth0', vmi)
        interface_register(vm, vmi, ifname)
        subprocess.check_output(
            'ip netns exec %s dhclient veth0' % args.container_id, shell=True)
    elif args.stop:
        vm = provisioner.virtual_machine_lookup(instance_name)

        vmi_list = vm.get_virtual_machine_interfaces()
        for ref in vmi_list:
            uuid = ref['uuid']
            interface_unregister(uuid)

        manager.clear_interfaces(args.container_id)

        for ref in vmi_list:
            provisioner.vmi_delete(ref['uuid'])

        provisioner.virtual_machine_delete(vm)
        subprocess.check_output(
            'ip netns delete %s' % args.container_id, shell=True)

if __name__ == '__main__':
    main()
