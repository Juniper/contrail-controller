#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from vnc_api.vnc_api import *
import uuid

def all (ip='10.84.9.45', port=8082, domain_name='my-domain',
        proj_name='my-proj', subnet1='192.168.1.0', subnet2='10.10.1.0',
        prefix1=30, prefix2=29, vn_name='my-fe', compute_node='a3s45.contrail.juniper.net'):
    vnc_lib = VncApi(username='admin', password='contrail123',
        tenant_name='admin', api_server_host = ip, api_server_port = port)

    # Object needs to be cleanup after the test to get same results for 
    # repetative test. Until objects are not cleanedup, change subnet before
    # testing it again.
    domain = Domain(domain_name)
    vnc_lib.domain_create(domain)
    print 'Created domain'

    project=Project(proj_name, domain)
    vnc_lib.project_create(project)
    print 'Created Project'

    ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
    vnc_lib.network_ipam_create(ipam)
    print 'Created network ipam'

    ipam = vnc_lib.network_ipam_read(fq_name = [domain_name, proj_name,
                    'default-network-ipam'])
    print 'Read network ipam'
    ipam_sn_1 = IpamSubnetType(subnet = SubnetType(subnet1, prefix1))
    ipam_sn_2 = IpamSubnetType(subnet = SubnetType(subnet2, prefix2))

    vn = VirtualNetwork(vn_name, project)
    vn.add_network_ipam (ipam, VnSubnetsType([ipam_sn_1, ipam_sn_2]))
    vnc_lib.virtual_network_create (vn)
    print 'Created Virtual Network object ', vn.uuid
    print 'Read no of instance ip for each subnet ["192.168.1.0/30", "10.10.1.0/29"]'
    subnet_list = ["192.168.1.0/30", "10.10.1.0/29"]
    result = vnc_lib.virtual_network_subnet_ip_count(vn, subnet_list)
    print 'Expected output: {"ip_count_list": [0, 0]}'
    print 'Actual output:', result

    net_obj = vnc_lib.virtual_network_read(id = vn.uuid)

    ip_obj1 = InstanceIp(name = str(uuid.uuid4()))
    ip_obj1.uuid = ip_obj1.name
    print 'Created Instance IP object 1 ', ip_obj1.uuid

    vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
    vm_inst_obj1.uuid = vm_inst_obj1.name
    vnc_lib.virtual_machine_create(vm_inst_obj1)

    id_perms = IdPermsType(enable = True)
    port_obj1 = VirtualMachineInterface(str(uuid.uuid4()), vm_inst_obj1, id_perms = id_perms)
    port_obj1.uuid = port_obj1.name

    port_obj1.set_virtual_network(vn)
    ip_obj1.set_virtual_machine_interface(port_obj1)
    ip_obj1.set_virtual_network(net_obj)
    port_id1 = vnc_lib.virtual_machine_interface_create(port_obj1)

    print 'Allocating an IP address for first VM'
    ip_id1 = vnc_lib.instance_ip_create(ip_obj1)
    ip_obj1 = vnc_lib.instance_ip_read(id = ip_id1)
    ip_addr1 = ip_obj1.get_instance_ip_address()
    print ' got IP Address for first instance', ip_addr1

    net_obj = vnc_lib.virtual_network_read(id = vn.uuid)
    result = vnc_lib.virtual_network_subnet_ip_count(vn, subnet_list)
    print 'Expected output: {"ip_count_list": [1, 0]}'
    print 'Actual output:', result

    net_obj = vnc_lib.virtual_network_read(id = vn.uuid)

    ip_obj2 = InstanceIp(name = str(uuid.uuid4()))
    ip_obj2.uuid = ip_obj2.name
    print 'Created Instance IP object 2', ip_obj2.uuid

    vm_inst_obj2 = VirtualMachine(str(uuid.uuid4()))
    vm_inst_obj2.uuid = vm_inst_obj2.name
    vnc_lib.virtual_machine_create(vm_inst_obj2)

    id_perms = IdPermsType(enable = True)
    port_obj2 = VirtualMachineInterface(str(uuid.uuid4()), vm_inst_obj2, id_perms = id_perms)
    port_obj2.uuid = port_obj2.name

    port_obj2.set_virtual_network(vn)
    ip_obj2.set_virtual_machine_interface(port_obj2)
    ip_obj2.set_virtual_network(net_obj)
    port_id2 = vnc_lib.virtual_machine_interface_create(port_obj2)

    print 'Allocating an IP address for Second VM'
    ip_id2 = vnc_lib.instance_ip_create(ip_obj2)
    ip_obj2 = vnc_lib.instance_ip_read(id = ip_id2)
    ip_addr2 = ip_obj2.get_instance_ip_address()
    print ' got IP Address for Second instance', ip_addr2

    net_obj = vnc_lib.virtual_network_read(id = vn.uuid)
    result = vnc_lib.virtual_network_subnet_ip_count(vn, subnet_list)
    print 'Expected output: {"ip_count_list": [1, 1]}'
    print 'Actual output:', result
    print result

if __name__ == '__main__':
    all ()
