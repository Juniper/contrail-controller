REST API Tutorial
=================
This tutorial will detail the steps necessary to create couple of virtual-networks
and associate a policy on them such that only http traffic can pass.

Create virtual-network and network-policy objects
-------------------------------------------------
To create virtual-networks *vn-red* and *vn-blue* and network-policy *policy-red-blue*:

Request for *vn-blue* create ::

    curl -X POST -H "Content-Type: application/json; charset=UTF-8" -d '{"virtual-network": {"parent_type": "project", "fq_name": ["default-domain", "admin", "vn-blue"], "network_ipam_refs": [{"attr": {"ipam_subnets": [{"subnet": {"ip_prefix": "10.1.1.0", "ip_prefix_len": 24}}]}, "to": ["default-domain", "default-project", "default-network-ipam"]}]}}' http://10.84.14.2:8082/virtual-networks

Response ::
    {"virtual-network": {"fq_name": ["default-domain", "admin", "vn-blue"], "parent_uuid": "df7649a6-3e2c-4982-b0c3-4b5038eef587", "parent_href": "http://10.84.14.2:8082/project/df7649a6-3e2c-4982-b0c3-4b5038eef587", "uuid": "8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "href": "http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "name": "vn-blue"}}


Request for *vn-red* create ::

    curl -X POST -H "Content-Type: application/json; charset=UTF-8" -d '{"virtual-network": {"parent_type": "project", "fq_name": ["default-domain", "admin", "vn-red"], "network_ipam_refs": [{"attr": {"ipam_subnets": [{"subnet": {"ip_prefix": "20.1.1.0", "ip_prefix_len": 24}}]}, "to": ["default-domain", "default-project", "default-network-ipam"]}]}}' http://10.84.14.2:8082/virtual-networks

Response ::

    {"virtual-network": {"fq_name": ["default-domain", "admin", "vn-red"], "parent_uuid": "df7649a6-3e2c-4982-b0c3-4b5038eef587", "parent_href": "http://10.84.14.2:8082/project/df7649a6-3e2c-4982-b0c3-4b5038eef587", "uuid": "47a91732-629b-4cbe-9aa5-45ba4d7b0e99", "href": "http://10.84.14.2:8082/virtual-network/47a91732-629b-4cbe-9aa5-45ba4d7b0e99", "name": "vn-red"}}

Request for *policy-red-blue* create ::

    curl -X POST -H "Content-Type: application/json; charset=UTF-8" -d '{"network-policy": {"parent_type": "project", "fq_name": ["default-domain", "admin", "policy-red-blue"], "network_policy_entries": {"policy_rule": [{"direction": "<>", "protocol": "tcp", "dst_addresses": [{"virtual_network": "default-domain:admin:vn-blue"}], "dst_ports": [{"start_port": 80, "end_port": 80}], "simple_action": "pass", "src_addresses": [{"virtual_network": "default-domain:admin:vn-red"}], "src_ports": [{"end_port": -1, "start_port": -1}]}] }}}' http://10.84.14.2:8082/network-policys

Response ::

    {"network-policy": {"fq_name": ["default-domain", "admin", "policy-red-blue"], "parent_uuid": "df7649a6-3e2c-4982-b0c3-4b5038eef587", "parent_href": "http://10.84.14.2:8082/project/df7649a6-3e2c-4982-b0c3-4b5038eef587", "uuid": "f215a3ec-5cbd-4310-91f4-7bbca52b27bd", "href": "http://10.84.14.2:8082/network-policy/f215a3ec-5cbd-4310-91f4-7bbca52b27bd", "name": "policy-red-blue"}}

Update virtual-networks to use the policy 
-----------------------------------------
To associate *policy-red-blue* to *vn-red* and *vn-blue* virtual-networks:

Request for *vn-blue* update ::

    curl -X PUT -H "Content-Type: application/json; charset=UTF-8" -d '{"virtual-network": {"fq_name": ["default-domain", "admin", "vn-blue"],"network_policy_refs": [{"to": ["default-domain", "admin", "policy-red-blue"], "attr":{"sequence":{"major":0, "minor": 0}}}]}}' http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee

Response ::

    {"virtual-network": {"href": "http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "uuid": "8c84ff8a-30ac-4136-99d9-f0d9662f3eee"}}

Request for *vn-red* update ::

    curl -X PUT -H "Content-Type: application/json; charset=UTF-8" -d '{"virtual-network": {"fq_name": ["default-doma"vn-red"],"network_policy_refs": [{"to": ["default-domain", "admin", "policy-red-blue"], "attr":{"sequence":{"major":0, "minor": 0}}}]}}' http://10.84.14.2:8082/virtual-network/47a91732-629b-4cbe-9aa5-45ba4d7b0e99

Response ::

    {"virtual-network": {"href": "http://10.84.14.2:8082/virtual-network/47a91732-629b-4cbe-9aa5-45ba4d7b0e99", "uuid": "47a91732-629b-4cbe-9aa5-45ba4d7b0e99"}}

Read the objects to verify
--------------------------
Request for *vn-blue* read ::

    curl -X GET -H "Content-Type: application/json; charset=UTF-8" http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee

Response ::

    {"virtual-network": {"virtual_network_properties": {"network_id": 4, "vxlan_network_identifier": null, "extend_to_external_routers": null}, "fq_name": ["default-domain", "admin", "vn-blue"], "uuid": "8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "access_control_lists": [{"to": ["default-domain", "admin", "vn-blue", "vn-blue"], "href": "http://10.84.14.2:8082/access-control-list/24b9c337-7be8-4883-a9a0-60197edf64e4", "uuid": "24b9c337-7be8-4883-a9a0-60197edf64e4"}], "network_policy_refs": [{"to": ["default-domain", "admin", "policy-red-blue"], "href": "http://10.84.14.2:8082/network-policy/f215a3ec-5cbd-4310-91f4-7bbca52b27bd", "attr": {"sequence": {"major": 0, "minor": 0}}, "uuid": "f215a3ec-5cbd-4310-91f4-7bbca52b27bd"}], "parent_uuid": "df7649a6-3e2c-4982-b0c3-4b5038eef587", "parent_href": "http://10.84.14.2:8082/project/df7649a6-3e2c-4982-b0c3-4b5038eef587", "parent_type": "project", "href": "http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "id_perms": {"enable": true, "description": null, "created": "2013-09-13T00:26:05.290644", "uuid": {"uuid_mslong": 10125498831222882614, "uuid_lslong": 11086156774262128366}, "last_modified": "2013-09-13T00:47:41.219833", "permissions": {"owner": "cloud-admin", "owner_access": 7, "other_access": 7, "group": "cloud-admin-group", "group_access": 7}}, "routing_instances": [{"to": ["default-domain", "admin", "vn-blue", "vn-blue"], "href": "http://10.84.14.2:8082/routing-instance/732567fd-8607-4045-b6c0-ff4109d3e0fb", "uuid": "732567fd-8607-4045-b6c0-ff4109d3e0fb"}], "network_ipam_refs": [{"to": ["default-domain", "default-project", "default-network-ipam"], "href": "http://10.84.14.2:8082/network-ipam/a01b486e-2c3e-47df-811c-440e59417ed8", "attr": {"ipam_subnets": [{"subnet": {"ip_prefix": "10.1.1.0", "ip_prefix_len": 24}, "default_gateway": "10.1.1.254"}]}, "uuid": "a01b486e-2c3e-47df-811c-440e59417ed8"}], "name": "vn-blue"}}

List the virtual-networks
-------------------------
To list the virtual networks:

Request ::

    curl -X GET -H "Content-Type: application/json; charset=UTF-8" http://10.84.14.2:8082/virtual-networks

Response ::

    {"virtual-networks": [{"href": "http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "fq_name": ["default-domain", "admin", "vn-blue"], "uuid": "8c84ff8a-30ac-4136-99d9-f0d9662f3eee"}, {"href": "http://10.84.14.2:8082/virtual-network/47a91732-629b-4cbe-9aa5-45ba4d7b0e99", "fq_name": ["default-domain", "admin", "vn-red"], "uuid": "47a91732-629b-4cbe-9aa5-45ba4d7b0e99"}, {"href": "http://10.84.14.2:8082/virtual-network/f423b6c8-deb6-4325-9035-15a8c8bb0a0d", "fq_name": ["default-domain", "default-project", "__link_local__"], "uuid": "f423b6c8-deb6-4325-9035-15a8c8bb0a0d"}, {"href": "http://10.84.14.2:8082/virtual-network/d44a51b0-f2d8-4644-aee0-fe856f970683", "fq_name": ["default-domain", "default-project", "default-virtual-network"], "uuid": "d44a51b0-f2d8-4644-aee0-fe856f970683"}, {"href": "http://10.84.14.2:8082/virtual-network/aad9e80a-8638-449f-a484-5d1bfd58065c", "fq_name": ["default-domain", "default-project", "ip-fabric"], "uuid": "aad9e80a-8638-449f-a484-5d1bfd58065c"}]}

Delete the objects
------------------
To delete the virtual-networks and network-policy objects created:

Request for *vn-red* delete ::

    curl -X DELETE -H "Content-Type: application/json; charset=UTF-8" http://10.84.14.2:8082/virtual-network/47a91732-629b-4cbe-9aa5-45ba4d7b0e99

Response *None*
