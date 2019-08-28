"""
"""

from builtins import object
from vnc_api.vnc_api import *


class Provisioner(object):
    def __init__(self, api_server='127.0.0.1', api_port=8082,
                 project='default-domain:default-project',
                 username=None, password=None):
        self._project = project
        self._client = VncApi(username=username, password=password,
                              tenant_name=self._project,
                              api_server_host=api_server,
                              api_server_port=api_port)

    def virtual_machine_lookup(self, vm_name):
        fq_name = [vm_name]
        try:
            vm_instance = self._client.virtual_machine_read(fq_name=fq_name)
            return vm_instance
        except NoIdError:
            pass
        return None

    def virtual_machine_locate(self, vr_name, vm_name):
        # if vm_name.find(':') == -1:
        #    vm_name = self._project + ':' + vm_name
        fq_name = vm_name.split(':')
        try:
            vm_instance = self._client.virtual_machine_read(fq_name=fq_name)
            return vm_instance
        except NoIdError:
            pass

        vm_instance = VirtualMachine(vm_name)
        self._client.virtual_machine_create(vm_instance)
        # set VR->VM ref since subscribe won't be sent for VM by agent
        vrouter = self._client.virtual_router_read(
                       fq_name=['default-global-system-config', vr_name])
        vrouter.add_virtual_machine(vm_instance)
        self._client.virtual_router_update(vrouter)
        return vm_instance

    def virtual_machine_delete(self, vr_name, vm_instance):
        # Delete VR->VM ref
        vrouter = self._client.virtual_router_read(
                       fq_name=['default-global-system-config', vr_name])
        vrouter.del_virtual_machine(vm_instance)
        self._client.virtual_router_update(vrouter)
        self._client.virtual_machine_delete(id=vm_instance.uuid)

    def _virtual_network_lookup(self, network_name):
        fq_name = network_name.split(':')
        try:
            vnet = self._client.virtual_network_read(fq_name=fq_name)
        except NoIdError:
            return None

        return vnet

    def project_lookup(self, fq_name):
        try:
            proj = self._client.project_read(fq_name=fq_name)
        except NoIdError:
            return None

        return proj

    def vmi_locate(self, vm_instance, network, name, advertise_default=True):
        vnet = self._virtual_network_lookup(network)
        if not vnet:
            sys.exit(1)

        project = self.project_lookup(fq_name=vnet.get_fq_name()[:-1])
        vmi_name = '%s-%s' %(vm_instance.name, name)
        vmi_fq_name = project.get_fq_name() + [vmi_name]
        create = False
        try:
            vmi = self._client.virtual_machine_interface_read(fq_name=vmi_fq_name)
        except NoIdError:
            vmi = VirtualMachineInterface(name=vmi_name, parent_obj=project)
            create = True

        vmi.set_virtual_network(vnet)
        vmi.set_virtual_machine(vm_instance)
        if create:
            self._client.virtual_machine_interface_create(vmi)
            vmi = self._client.virtual_machine_interface_read(id=vmi.uuid)
        else:
            self._client.virtual_machine_interface_update(vmi)

        ips = vmi.get_instance_ip_back_refs()
        if ips and len(ips):
            uuid = ips[0]['uuid']
        else:
            ip = InstanceIp(vm_instance.name + '.' + name)
            ip.set_virtual_machine_interface(vmi)
            ip.set_virtual_network(vnet)
            uuid = self._client.instance_ip_create(ip)

        ip = self._client.instance_ip_read(id=uuid)

        # print "IP address: %s" % ip.get_instance_ip_address()
        return vmi, project

    def vmi_delete(self, uuid):
        try:
            vmi = self._client.virtual_machine_interface_read(id=uuid)
        except NoIdError:
            return

        ips = vmi.get_instance_ip_back_refs()
        for ref in ips:
            self._client.instance_ip_delete(id=ref['uuid'])

        self._client.virtual_machine_interface_delete(id=vmi.uuid)

    def _get_vmi_prefixlen(self, vmi):
        refs = vmi.get_virtual_network_refs()
        if len(refs) == 0:
            sys.exit(1)

        vnet = self._client.virtual_network_read(id=refs[0]['uuid'])
        ipam_r = vnet.get_network_ipam_refs()
        return ipam_r[0]['attr'].ipam_subnets[0].subnet.ip_prefix_len

    def get_interface_ip_prefix(self, vmi):
        ips = vmi.get_instance_ip_back_refs()
        if len(ips) == 0:
            return None
        ip_obj = self._client.instance_ip_read(id=ips[0]['uuid'])
        ip_addr = ip_obj.get_instance_ip_address()
        ip_prefixlen = self._get_vmi_prefixlen(vmi)
        return (ip_addr, ip_prefixlen)
