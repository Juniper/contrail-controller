#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import uuid

from vnc_api import *

def example_1():
    #_TENANT_NAME = "infra"
    lib = VncApi('user1', 'password1')
    
    proj_1_obj = Project('project-1')
    proj_1_uuid = lib.project_create(proj_1_obj)
    
    dhcp_1_options = [{'dhcp_option_name': 'opt1', 'dhcp_option_value': 'opt1_value'},
                      {'dhcp_option_name': 'opt2', 'dhcp_option_value': 'opt2_value'}]
    dhcp_1_options_list =  DhcpOptionsListType(dhcp_1_options)
    ipam_data = IpamType.factory(ipam_method = 'dhcp',
                                 dhcp_option_list = dhcp_1_options_list)
    netipam_1_obj = NetworkIpam('network-ipam-1', proj_1_obj, ipam_data)
    netipam_1_uuid = lib.network_ipam_create(netipam_1_obj)

    dhcp_2_options = [{'dhcp_option': 'opt3', 'dhcp_option_value': 'opt3_value'},
                      {'dhcp_option': 'opt4', 'dhcp_option_value': 'opt4_value'}]
    netipam_2_obj = NetworkIpam('network-ipam-2', proj_1_obj,
                                IpamType('dhcp', dhcp_2_options))
    netipam_2_uuid = lib.network_ipam_create(netipam_2_obj)
    
    netipam_obj = lib.network_ipam_read(id = netipam_1_uuid)
    
    vnet_1_obj = VirtualNetwork('virtual-network-1', proj_1_obj)
    
    sn_1 = VnSubnetsType([SubnetType('10.1.1.0', 24)])
    sn_2 = VnSubnetsType([SubnetType('20.1.1.0', 24)])
    
    vnet_1_uuid = lib.virtual_network_create(vnet_1_obj)
    
    vnet_obj = lib.virtual_network_read(id = vnet_1_uuid)
    
    vnet_obj.add_network_ipam(netipam_1_obj, sn_1)
    lib.virtual_network_update(vnet_obj)
    vnet_obj.add_network_ipam(netipam_2_obj, sn_2)
    lib.virtual_network_update(vnet_obj)
    
    nets_info = lib.virtual_networks_list(proj_1_obj)
    
    vnet_2_obj = VirtualNetwork('virtual-network-2', proj_1_obj)
    vnet_2_uuid = lib.virtual_network_create(vnet_2_obj)
    
    policy_1_obj = NetworkPolicy('policy-1', proj_1_obj)
    policy_1_uuid = lib.network_policy_create(policy_1_obj)
    acl_1_rules = [ AclRuleType('inbound', MatchConditionType(protocol = 'tcp'), ActionListType(True)),
                   AclRuleType('outbound', MatchConditionType(protocol = 'udp'), ActionListType(False))
                 ]
    acl_1_entries = AclEntriesType(acl_1_rules)
    acl_1_obj = AccessControlList('acl-1', policy_1_obj)
    acl_1_uuid = lib.access_control_list_create(acl_1_obj)
    
    vnet_1_obj.set_network_policy(policy1_obj)
    lib.virtual_network_update(net_1_obj)
#end example_1

def example_2():
    lib = VncApi('user1', 'password1')
    
    # Configuration 
    proj_1_obj = Project('project-1')
    proj_1_uuid = lib.project_create(proj_1_obj)
    
    netipam_1_obj = NetworkIpam('network-ipam-1', proj_1_obj, IpamType("dhcp"))
    netipam_1_uuid = lib.network_ipam_create(netipam_1_obj)
    netipam_obj = lib.network_ipam_read(id = netipam_1_uuid)
    
    vnet_1_obj = VirtualNetwork('virtual-network-1', proj_1_obj)
    vnet_1_uuid = lib.virtual_network_create(vnet_1_obj)
    sn_1 = VnSubnetsType([SubnetType('10.1.1.0', 24)])
    vnet_obj = lib.virtual_network_read(id = vnet_1_uuid)
    vnet_obj.add_network_ipam(netipam_1_obj, sn_1)
    lib.virtual_network_update(vnet_obj)
    
    
    vnet_2_obj = VirtualNetwork('virtual-network-2', proj_1_obj)
    vnet_2_uuid = lib.virtual_network_create(vnet_2_obj)
    sn_2 = VnSubnetsType([SubnetType('20.1.1.0', 24)])
    vnet_obj = lib.virtual_network_read(id = vnet_2_uuid)
    vnet_obj.add_network_ipam(netipam_1_obj, sn_2)
    lib.virtual_network_update(vnet_obj)
    
    # Operational
    server_1_obj = VirtualRouter('phys-host-1')
    server_1_uuid = lib.virtual_router_create(server_1_obj)
    instance_1_obj = VirtualMachine(str(uuid.uuid4()))
    instance_1_uuid = lib.virtual_machine_create(instance_1_obj)
    port_1_obj = VirtualMachineInterface(str(uuid.uuid4()), instance_1_obj)
    port_1_obj.set_virtual_network(vnet_1_obj)
    port_1_uuid = lib.virtual_machine_interface_create(port_1_obj)
    server_1_obj.set_virtual_machine(instance_1_obj)
    lib.virtual_router_update(server_1_obj)
    
    
    server_2_obj = VirtualRouter('phys-host-2')
    server_2_uuid = lib.virtual_router_create(server_2_obj)
    instance_2_obj = VirtualMachine(str(uuid.uuid4()))
    instance_2_uuid = lib.virtual_machine_create(instance_2_obj)
    port_2_obj = VirtualMachineInterface(str(uuid.uuid4()), instance_2_obj)
    port_2_obj.set_virtual_network(vnet_2_obj)
    port_2_uuid = lib.virtual_machine_interface_create(port_2_obj)
    
    server_2_obj.set_virtual_machine(instance_2_obj)
    lib.virtual_router_update(server_2_obj)
#end example_2

def example_3():
    lib = VncApi('u','p')
    net1=VirtualNetwork('vn1')
    net1_id = lib.virtual_network_create(net1)
    net1=lib.virtual_network_read(id=net1_id)

    net2=VirtualNetwork('vn2')
    net2_id = lib.virtual_network_create(net2)
    net2=lib.virtual_network_read(id=net2_id)

    np_rules = [PolicyRuleType(None, '<>', 'pass', 'any',
                    [AddressType(virtual_network = 'local')], [PortType(-1, -1)], None,
                    [AddressType(virtual_network = net2.get_fq_name_str())], [PortType(-1, -1)], None)]
    np_entries = PolicyEntriesType(np_rules)
    pol1=NetworkPolicy('np1', network_policy_entries = np_entries)
    pol1_id = lib.network_policy_create(pol1)
    net1.add_network_policy(pol1, SequenceType(1, 0))
    lib.virtual_network_update(net1)

    np_rules = [PolicyRuleType(None, '<>', 'pass', 'any',
                    [AddressType(virtual_network = 'local')], [PortType(-1, -1)], None,
                    [AddressType(virtual_network = net1.get_fq_name_str())], [PortType(-1, -1)], None)]
    np_entries = PolicyEntriesType(np_rules)
    pol2=NetworkPolicy('np2', network_policy_entries = np_entries)
    pol2_id = lib.network_policy_create(pol2)
    net2.add_network_policy(pol2, SequenceType(1, 0))
    lib.virtual_network_update(net2)

    print lib.virtual_network_read(id = net1_id)
    print lib.virtual_network_read(id = net2_id)
    print lib.virtual_networks_list(project_fq_name = [u'default-domain', u'default-project'])
    #lib.virtual_network_delete(id=net1_id)
    #print lib.virtual_networks_list()
#end example_3

example_3()
