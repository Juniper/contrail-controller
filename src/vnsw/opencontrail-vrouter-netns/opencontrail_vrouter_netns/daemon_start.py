"""
This script creates and configures a linux network namespace such that an
application can be executed under the context of a virtualized network.
"""
from __future__ import absolute_import

import argparse
import socket
import sys

from .instance_provisioner import Provisioner
from .lxc_manager import LxcManager
from .vrouter_control import interface_register


def build_network_name(project_name, network_name):
    if network_name.find(':') >= 0:
        return network_name
    return "%s:%s" % (project_name, network_name)

def get_project_name(project_name, network_name):
    network_fq_name = network_name.split(':')
    if len(network_fq_name) >= 0:
        return network_fq_name[-2]
    else:
        project_fq_name = project_name.split(':')
        return project_fq_name[-1]

def daemon_start():
    """
    Creates a virtual-machine and vmi object in the API server.
    Creates a namespace and a veth interface pair.
    Associates the veth interface in the master instance with the vrouter.
    """
    parser = argparse.ArgumentParser()
    defaults = {
        'username': None,
        'password': None,
        'api-server': '127.0.0.1',
        'api-port': 8082,
        'project': 'default-domain:default-project',
        'network': 'default-network',
        'monitor' : False,
    }
    parser.set_defaults(**defaults)
    parser.add_argument("-U", "--username", help="Username of the tenant")
    parser.add_argument("-P", "--password", help="Password for the user")
    parser.add_argument("-s", "--api-server", help="API server address")
    parser.add_argument("-p", "--api-port", type=int, help="API server port")
    parser.add_argument("--project", help="OpenStack project name")
    parser.add_argument("-n", "--network", help="Primary network")
    parser.add_argument("-o", "--outbound", help="Outbound traffic network")
    '''
    --monitor is maintained only for backward compatibility. Replug of vif's
    across vrouter-agent is not required anymore as vif info is persisted in
    files which are read upon vrouter-agent start.
    '''
    parser.add_argument("-M", "--monitor", action='store_true',
                        help="Monitor the vrouter agent connection to replug the vif's.")
    parser.add_argument("daemon", help="Deamon Name")

    arguments = parser.parse_args(sys.argv[1:])

    manager = LxcManager()
    project_name = get_project_name(arguments.project, arguments.network)
    provisioner = Provisioner(username=arguments.username,
                              password=arguments.password,
                              api_server=arguments.api_server,
                              api_port=arguments.api_port,
                              project=project_name)
    vrouter_name = socket.getfqdn()
    instance_name = '%s-%s' % (vrouter_name, arguments.daemon)
    vm = provisioner.virtual_machine_locate(vrouter_name, instance_name)

    network = build_network_name(arguments.project, arguments.network)

    vmi, project = provisioner.vmi_locate(vm, network, 'veth0')
    vmi_out = None
    if arguments.outbound:
        outbound_name = build_network_name(arguments.project,
                                           arguments.outbound)
        vmi_out, project = provisioner.vmi_locate(vm, outbound_name, 'veth1')

    manager.namespace_init(arguments.daemon)
    ifname = manager.interface_update(arguments.daemon, vmi, 'veth0')
    interface_register(vm, vmi, ifname, project=project)

    vmi_out_kwargs = {}
    if vmi_out:
        ifname = manager.interface_update(arguments.daemon, vmi_out, 'veth1')
        interface_register(vm, vmi_out, ifname, project=project)
        vmi_out_kwargs = {'vmi_out' : vmi_out,
                          'ifname_out' : ifname
                         }

    single_interface = (arguments.outbound is None)
    ip_prefix = provisioner.get_interface_ip_prefix(vmi)
    manager.interface_config(arguments.daemon, 'veth0',
                             advertise_default=single_interface,
                             ip_prefix=ip_prefix)
    if vmi_out:
        manager.interface_config(arguments.daemon, 'veth1')

# end daemon_start


if __name__ == '__main__':
    daemon_start()
