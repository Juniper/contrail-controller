#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from vnc_api.vnc_api import *
import uuid


def all(ip='10.84.13.34', port=8082, domain_name='my-domain',
        proj_name='my-proj', subnet='192.168.1.0', prefix=24, vn_name='my-fe',
        compute_node='a2s3.contrail.juniper.net'):
    vnc_lib = VncApi(username='admin', password='contrail123',
                     tenant_name='admin', api_server_host=ip,
                     api_server_port=port)

    domain = Domain(domain_name)
    vnc_lib.domain_create(domain)
    print 'Created domain'

    project = Project(proj_name, domain)
    vnc_lib.project_create(project)
    print 'Created Project'

    ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
    vnc_lib.network_ipam_create(ipam)
    print 'Created network ipam'

    ipam = vnc_lib.network_ipam_read(fq_name=[domain_name, proj_name,
                                              'default-network-ipam'])
    print 'Read network ipam'
    ipam_sn_1 = IpamSubnetType(subnet=SubnetType(subnet, prefix))

    vn = VirtualNetwork(vn_name, project)
    vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_1]))
    vnc_lib.virtual_network_create(vn)
    net_obj = vnc_lib.virtual_network_read(id=vn.uuid)

    ip_obj = InstanceIp(name=str(uuid.uuid4()))
    ip_obj.uuid = ip_obj.name
    print 'Created Instance IP object ', ip_obj.uuid

    vrouter_obj = VirtualRouter(compute_node)
    vnc_lib.virtual_router_create(vrouter_obj)
    print 'Created Virtual Router object'

    vm_inst_obj = VirtualMachine(str(uuid.uuid4()))
    vm_inst_obj.uuid = vm_inst_obj.name
    vnc_lib.virtual_machine_create(vm_inst_obj)

    vrouter_obj.add_virtual_machine(vm_inst_obj)
    vnc_lib.virtual_router_update(vrouter_obj)

    id_perms = IdPermsType(enable=True)
    port_obj = VirtualMachineInterface(
        str(uuid.uuid4()), vm_inst_obj, id_perms=id_perms)
    port_obj.uuid = port_obj.name

    port_obj.set_virtual_network(vn)
    ip_obj.set_virtual_machine_interface(port_obj)
    ip_obj.set_virtual_network(net_obj)
    port_id = vnc_lib.virtual_machine_interface_create(port_obj)

    print 'Allocating an IP address'
    ip_id = vnc_lib.instance_ip_create(ip_obj)
    ip_obj = vnc_lib.instance_ip_read(id=ip_id)
    ip_addr = ip_obj.get_instance_ip_address()
    print ' got ', ip_addr

    print
    print 'Try to reserve above address ... should fail'
    try:
        ip_obj.set_instance_ip_address(ip_addr)
        ip_id = vnc_lib.instance_ip_create(ip_obj)
    except:
        print ' Failed to reserver already allocated IP address. Test passed'

    askip = '192.168.1.93'
    print
    print 'Try to reserve unassigned address %s ... should succeed' % (askip)
    ip_obj.set_instance_ip_address(askip)
    ip_id = vnc_lib.instance_ip_create(ip_obj)
    ip_obj = vnc_lib.instance_ip_read(id=ip_id)
    ip_addr = ip_obj.get_instance_ip_address()
    if ip_addr == askip:
        print ' Test passed!'
    else:
        print ' Test failed! got %s' % (ip_addr)

if __name__ == '__main__':
    all()
