"""
Unconfigure the namespace created by daemon_start.
"""
from __future__ import absolute_import

import argparse
import socket
import sys

from .instance_provisioner import Provisioner
from .lxc_manager import LxcManager
from .vrouter_control import interface_unregister


def daemon_stop():
    parser = argparse.ArgumentParser()
    defaults = {
        'username': None,
        'password': None,
        'api-server': '127.0.0.1',
        'api-port': 8082,
        'project': 'default-project',
    }
    parser.set_defaults(**defaults)
    parser.add_argument("-U", "--username", help="Username of the tenant")
    parser.add_argument("-P", "--password", help="Password for the user")
    parser.add_argument("-s", "--api-server", help="API server address")
    parser.add_argument("-p", "--api-port", type=int, help="API server port")
    parser.add_argument("--project", help="OpenStack project name")
    parser.add_argument("daemon", help="Deamon Name")
    arguments = parser.parse_args(sys.argv[1:])

    manager = LxcManager()
    provisioner = Provisioner(username=arguments.username,
                              password=arguments.password,
                              project=arguments.project,
                              api_server=arguments.api_server,
                              api_port=arguments.api_port)
    vrouter_name = socket.getfqdn()
    instance_name = '%s-%s' % (vrouter_name, arguments.daemon)
    vm = provisioner.virtual_machine_lookup(instance_name)

    vmi_list = vm.get_virtual_machine_interface_back_refs()
    for ref in vmi_list:
        uuid = ref['uuid']
        interface_unregister(uuid)

    manager.clear_interfaces('ns-%s' % arguments.daemon)

    for ref in vmi_list:
        provisioner.vmi_delete(ref['uuid'])

    provisioner.virtual_machine_delete(vrouter_name, vm)
    manager.namespace_delete(arguments.daemon)

# end daemon_stop


if __name__ == '__main__':
    daemon_stop()
