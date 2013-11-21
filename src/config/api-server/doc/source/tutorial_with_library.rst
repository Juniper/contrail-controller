Library API Tutorial
====================
This tutorial will detail the steps necessary to create couple of virtual-networks
and associate a policy on them such that only http traffic can pass. The next section
generalizes the examples mentioned in this tutorial.

Initialize the library
----------------------
A single import of the vnc_api module from vnc_api package is sufficient to
interact with the configuration API server.

    >>> from vnc_api import vnc_api
    >>> vnc_lib = vnc_api.VncApi(api_server_host='10.84.14.2')

Create virtual-network and network-policy objects
-------------------------------------------------
To create virtual-networks *vn-red* and *vn-blue* and network-policy *policy-red-blue*

    >>> vn_blue_obj = vnc_api.VirtualNetwork('vn-blue')
    >>> vn_blue_obj.add_network_ipam(vnc_api.NetworkIpam(),
    ...                              vnc_api.VnSubnetsType([vnc_api.IpamSubnetType(subnet = vnc_api.SubnetType('10.1.1.0', 24))]))
    >>> vnc_lib.virtual_network_create(vn_blue_obj)
    u'57603abb-0089-4a89-b44b-8ca71d4b7826'

    >>> vn_red_obj = vnc_api.VirtualNetwork('vn-red')
    >>> vn_red_obj.add_network_ipam(vnc_api.NetworkIpam(),
    ...                         vnc_api.VnSubnetsType([vnc_api.IpamSubnetType(subnet = vnc_api.SubnetType('20.1.1.0', 24))]))
    >>> vnc_lib.virtual_network_create(vn_red_obj)
    u'5de3af3e-269f-40be-b0f6-69d6bb962a9f'


    >>> policy_obj = vnc_api.NetworkPolicy('policy-red-blue',
    ...                  network_policy_entries = vnc_api.PolicyEntriesType([vnc_api.PolicyRuleType(direction='<>',
    ...                      action_list = vnc_api.ActionListType(simple_action='pass'), protocol = 'tcp',
    ...                      src_addresses = [vnc_api.AddressType(virtual_network = vn_blue_obj.get_fq_name_str())],
    ...                      src_ports = [vnc_api.PortType(-1, -1)],
    ...                      dst_addresses = [vnc_api.AddressType(virtual_network = vn_red_obj.get_fq_name_str())],
    ...                      dst_ports = [vnc_api.PortType(80, 80)])]))
    >>> vnc_lib.network_policy_create(policy_obj)
    u'51388604-c59e-4169-9e0f-39bfcdc603f0'

Update virtual-networks to use the policy 
-----------------------------------------
To associate the *policy-red-blue* with the virtual networks:

    >>> vn_blue_obj.add_network_policy(policy_obj, vnc_api.VirtualNetworkPolicyType(
                                                       sequence=vnc_api.SequenceType(0, 0)))
    >>> vn_red_obj.add_network_policy(policy_obj, vnc_api.VirtualNetworkPolicyType(
                                                       sequence=vnc_api.SequenceType(0, 0)))
    >>> vnc_lib.virtual_network_update(vn_blue_obj)
    u'{"virtual-network": {"href": "http://10.84.14.2:8082/virtual-network/57603abb-0089-4a89-b44b-8ca71d4b7826", "uuid": "57603abb-0089-4a89-b44b-8ca71d4b7826"}}'
    >>> vnc_lib.virtual_network_update(vn_red_obj)
    u'{"virtual-network": {"href": "http://10.84.14.2:8082/virtual-network/5de3af3e-269f-40be-b0f6-69d6bb962a9f", "uuid": "5de3af3e-269f-40be-b0f6-69d6bb962a9f"}}'

Reading the objects to verify
-----------------------------
An object can be read by using its uuid returned by create...    

    >>> print vnc_lib.virtual_network_read(id = vn_blue_obj.uuid)

... or by its fully-qualified name.

    >>> print vnc_lib.virtual_network_read(fq_name = ['default-domain', 'default-project', 'vn-blue'])

List the virtual-networks
-------------------------
A collection of objects can be listed by 

    >>> print vnc_lib.virtual_networks_list()

Deleting the objects
--------------------
An object can be deleted using its uuid...

    >>> vnc_lib.virtual_network_delete(id = '57603abb-0089-4a89-b44b-8ca71d4b7826')

... or by its fully-qualified name

    >>> vnc_lib.virtual_network_delete(fq_name = ['default-domain', 'default-project', 'vn-blue'])

