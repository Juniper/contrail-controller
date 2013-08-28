REST API Tutorial
=================
This tutorial will detail the steps necessary to create couple of virtual-networks
and associate a policy on them such that only http traffic can pass.

Create virtual-network and network-policy objects
-------------------------------------------------
To create virtual-networks *vn-red* and *vn-blue* and network-policy *policy-red-blue*:

Request for *vn-blue* create ::

    curl -X POST -H "Content-Type: application/json; charset=UTF-8" -d '{"virtual-network": {"fq_name": ["default-domain", "admin", "vn-blue"], "network_ipam_refs": [{"attr": {"ipam_subnets": [{"subnet": {"ip_prefix": "10.1.1.0", "ip_prefix_len": 24}}]}, "to": ["default-domain", "admin", "default-network-ipam"]}]}}' http://10.84.14.2:8082/virtual-networks

Response ::

    {"virtual-network": {"href": "http://10.84.14.2:8082/virtual-network/c07f0ecf-bfb6-4fbe-8f01-45580027d25b", "fq_name": ["default-domain", "admin", "vn-blue"], "name": "vn-blue", "uuid": "c07f0ecf-bfb6-4fbe-8f01-45580027d25b"}}

Request for *vn-red* create ::

    curl -X POST -H "Content-Type: application/json; charset=UTF-8" -d '{"virtual-network": {"fq_name": ["default-domain", "admin", "vn-red"], "network_ipam_refs": [{"attr": {"ipam_subnets": [{"subnet": {"ip_prefix": "20.1.1.0", "ip_prefix_len": 24}}]}, "to": ["default-domain", "admin", "default-network-ipam"]}]}}' http://10.84.14.2:8082/virtual-networks

Response ::

    {"virtual-network": {"href": "http://10.84.14.2:8082/virtual-network/a405bdac-97d2-4a6e-bdef-e83ab9a4ce61", "fq_name": ["default-domain", "admin", "vn-red"], "name": "vn-red", "uuid": "a405bdac-97d2-4a6e-bdef-e83ab9a4ce61"}}

Request for *policy-red-blue* create ::

    curl -X POST -H "Content-Type: application/json; charset=UTF-8" -d '{"network-policy": {"fq_name": ["default-domain", "admin", "policy-red-blue"], "network_policy_entries": {"policy_rule": [{"direction": "<>", "protocol": "tcp", "dst_addresses": [{"virtual_network": "default-domain:admin:vn-blue"}], "dst_ports": [{"start_port": 80, "end_port": 80}], "simple_action": "pass", "src_addresses": [{"virtual_network": "default-domain:admin:vn-red"}], "src_ports": [{"end_port": -1, "start_port": -1}]}] }}}' http://10.84.14.2:8082/network-policys

Response ::

    {"network-policy": {"href": "http://10.84.14.2:8082/network-policy/507ce7ff-e277-4508-937b-b18c6fd087e9", "fq_name": ["default-domain", "admin", "policy-red-blue"], "name": "policy-red-blue", "uuid": "507ce7ff-e277-4508-937b-b18c6fd087e9"}}


Update virtual-networks to use the policy 
-----------------------------------------
To associate *policy-red-blue* to *vn-red* and *vn-blue* virtual-networks:

Request for *vn-blue* update ::

    curl -X PUT -H "Content-Type: application/json; charset=UTF-8" -d '{"virtual-network": {"network_policy_refs": [{"to": ["default-domain", "admin", "policy-red-blue"], "attr":null}]}}' http://10.84.14.2:8082/virtual-network/c07f0ecf-bfb6-4fbe-8f01-45580027d25b

Response ::

    {"virtual-network": {"href": "http://10.84.14.2:8082/virtual-network/c07f0ecf-bfb6-4fbe-8f01-45580027d25b", "uuid": "c07f0ecf-bfb6-4fbe-8f01-45580027d25b"}}

Request for *vn-red* update ::

    curl -X PUT -H "Content-Type: application/json; charset=UTF-8" -d '{"virtual-network": {"network_policy_refs": [{"to": ["default-domain", "admin", "policy-red-blue"], "attr":null}]}}' http://10.84.14.2:8082/virtual-network/a405bdac-97d2-4a6e-bdef-e83ab9a4ce61

Response ::

    {"virtual-network": {"href": "http://10.84.14.2:8082/virtual-network/a405bdac-97d2-4a6e-bdef-e83ab9a4ce61", "uuid": "a405bdac-97d2-4a6e-bdef-e83ab9a4ce61"}}

Read the objects to verify
--------------------------
Request for *vn-red* read ::

    curl -X GET -H "Content-Type: application/json; charset=UTF-8" http://10.84.14.2:8082/virtual-network/a405bdac-97d2-4a6e-bdef-e83ab9a4ce61

Response ::

    {"virtual-network": {"parent_name": "admin", "_type": "virtual-network", "fq_name": ["default-domain", "admin", "vn-red"], "uuid": "a405bdac-97d2-4a6e-bdef-e83ab9a4ce61", "network_policy_refs": [{"to": ["default-domain", "admin", "policy-red-blue"], "href": "http://10.84.14.2:8082/network-policy/507ce7ff-e277-4508-937b-b18c6fd087e9", "attr": {"timer": null, "sequence": null}, "uuid": "507ce7ff-e277-4508-937b-b18c6fd087e9"}], "parent_uuid": "15b7f777-14a3-4b1f-86fb-90b96439e55a", "parent_href": "http://10.84.14.2:8082/project/15b7f777-14a3-4b1f-86fb-90b96439e55a", "href": "http://10.84.14.2:8082/virtual-network/a405bdac-97d2-4a6e-bdef-e83ab9a4ce61", "id_perms": {"enable": true, "description": null, "created": null, "uuid": {"uuid_mslong": 11819061346082900590, "uuid_lslong": 13686413131522559585}, "last_modified": null, "permissions": {"owner": "cloud-admin", "owner_access": 7, "other_access": 7, "group": "cloud-admin-group", "group_access": 7}}, "name": "vn-red"}}

List the virtual-networks
-------------------------
To list the virtual networks:

Request ::

    curl -X GET -H "Content-Type: application/json; charset=UTF-8" http://10.84.14.2:8082/virtual-networks?parent_id=15b7f777-14a3-4b1f-86fb-90b96439e55a

*OR* ::

    curl -X GET -H "Content-Type: application/json; charset=UTF-8" http://10.84.14.2:8082/virtual-networks?parent_fq_name_str='default-domain:default-project'

Response ::

    {"virtual-networks": [{"href": "http://10.84.14.2:8082/virtual-network/5bfe13f1-f59e-42a8-b45e-5014671da1e2", "fq_name": ["default-domain", "admin", "default-virtual-network"], "uuid": "5bfe13f1-f59e-42a8-b45e-5014671da1e2"}, {"href": "http://10.84.14.2:8082/virtual-network/a405bdac-97d2-4a6e-bdef-e83ab9a4ce61", "fq_name": ["default-domain", "admin", "vn-red"], "uuid": "a405bdac-97d2-4a6e-bdef-e83ab9a4ce61"}, {"href": "http://10.84.14.2:8082/virtual-network/c07f0ecf-bfb6-4fbe-8f01-45580027d25b", "fq_name": ["default-domain", "admin", "vn-blue"], "uuid": "c07f0ecf-bfb6-4fbe-8f01-45580027d25b"}]}

Delete the objects
------------------
To delete the virtual-networks and network-policy objects created:

Request for *vn-red* delete ::

    curl -X DELETE -H "Content-Type: application/json; charset=UTF-8" http://10.84.14.2:8082/virtual-network/a405bdac-97d2-4a6e-bdef-e83ab9a4ce61

Response *None*
