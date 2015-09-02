import sys
import argparse

from vnc_api.vnc_api import *

class Provisioner(object):
    def __init__(self, arguments):
        self._client = VncApi(api_server_host=arguments.api_server,
                              api_server_port=arguments.port)
        self._network_name = arguments.network
        self._project = arguments.project
        self._subnet = arguments.subnet

    def virtual_machine_lookup(self, vm_name):
        fq_name = [vm_name]
        try:
            vm_instance = self._client.virtual_machine_read(fq_name = fq_name)
            return vm_instance
        except NoIdError:
            pass
        return None

    def virtual_machine_locate(self, vm_name):
        fq_name = [vm_name]
        try:
            vm_instance = self._client.virtual_machine_read(fq_name = fq_name)
            return vm_instance
        except NoIdError:
            pass

        vm_instance = VirtualMachine(vm_name)
        self._client.virtual_machine_create(vm_instance)
        return vm_instance

    def virtual_machine_delete(self, vm_instance):
        self._client.virtual_machine_delete(id = vm_instance.uuid)

    def _virtual_network_locate(self):
        fq_name = self._network_name.split(':')
        try:
            vnet = self._client.virtual_network_read(fq_name = fq_name)
            return vnet
        except NoIdError:
            pass

        if not self._subnet:
            print "%s does not exist" %  self._network_name
            print "Please specify a subnet IP address in order to create virtual-network"
            return None

        vnet = VirtualNetwork(fq_name[-1], parent_type = 'project',
                              fq_name = fq_name)

        ipam = self._client.network_ipam_read(
            fq_name = ['default-domain',
                       'default-project',
                       'default-network-ipam'])

        (prefix, plen) = self._subnet.split('/')
        subnet = IpamSubnetType(subnet = SubnetType(prefix, int(plen)))
        vnet.add_network_ipam(ipam, VnSubnetsType([subnet]))

        self._client.virtual_network_create(vnet)
        return vnet

    def vmi_update(self, vm_instance):
        fq_name = vm_instance.fq_name
        fq_name.append('0')
        create = False
        try:
            vmi = self._client.virtual_machine_interface_read(fq_name = fq_name)
        except NoIdError:
            vmi = VirtualMachineInterface(parent_type = 'virtual-machine',
                                          fq_name = fq_name)
            create = True

        vnet = self._virtual_network_locate()
        if not vnet:
            sys.exit(1)

        vmi.set_virtual_network(vnet)
        if create:
            self._client.virtual_machine_interface_create(vmi)
        else:
            self._client.virtual_machine_interface_update(vmi)

        ips = vmi.get_instance_ip_back_refs()
        if ips and len(ips):
            uuid = ips[0]['uuid']
        else:
            ip = InstanceIp(vm_instance.name + '.0')
            ip.set_virtual_machine_interface(vmi)
            ip.set_virtual_network(vnet)
            uuid = self._client.instance_ip_create(ip)

        ip = self._client.instance_ip_read(id=uuid)

        print "IP address: %s" % ip.get_instance_ip_address()
        return vmi

    def vmi_clean(self, vm_instance):
        fq_name = vm_instance.fq_name
        fq_name.append('0')
        try:
            vmi = self._client.virtual_machine_interface_read(fq_name = fq_name)
        except NoIdError:
            return

        ips = vmi.get_instance_ip_back_refs()
        for ref in ips:
            self._client.instance_ip_delete(id = ref['uuid'])

        self._client.virtual_machine_interface_delete(id = vmi.uuid)

def instance_config(instance_name, arguments):
    provisioner = Provisioner(arguments)
    vm_instance = provisioner.virtual_machine_locate(instance_name)
    provisioner.vmi_update(vm_instance)

def instance_unconfig(instance_name, arguments):
    provisioner = Provisioner(arguments)
    vm_instance = provisioner.virtual_machine_lookup(instance_name)
    if vm_instance:
        provisioner.vmi_clean(vm_instance)
        provisioner.virtual_machine_delete(vm_instance)

def main(argv):
    parser = argparse.ArgumentParser()
    defaults = {
        'api-server': '127.0.0.1',
        'port': '8082',
        'network': 'default-domain:default-project:default-network',
        'project': 'default-domain:default-project'
        }
    parser.set_defaults(**defaults)
    parser.add_argument(
        "-s", "--api-server", help="API server address")
    parser.add_argument(
        "-p", "--port", help="API server port")
    parser.add_argument(
        "-n", "--network", help="Virtual-network")
    parser.add_argument(
        "--subnet", help="IP subnet address for the virtual-network")
    parser.add_argument(
        "--project", help="OpenStack project name")
    parser.add_argument(
        "--add", action="store_true", help="Add instance")
    parser.add_argument(
        "--delete", action="store_true", help="Delete instance")
    parser.add_argument(
        "instance", help="Instance name")
    arguments = parser.parse_args(argv)
    if arguments.add:
        instance_config(arguments.instance, arguments)
    elif arguments.delete:
        instance_unconfig(arguments.instance, arguments)
    else:
        print "Please specify one of --add or --delete"
        sys.exit(1)

if __name__ == '__main__':
    main(sys.argv[1:])
