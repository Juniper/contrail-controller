REST API Details
================
The configuration API server provides a means of accessing and manipulating configuration
elements of the system using HTTP operations on resources represented in JSON.

The configuration element types (also referred to as resource types) have a hierarchical relationship 
described in :doc:`vnc_cfg.xsd` schema. JSON representation of these objects are what is 
expected on the wire.

For each resource type, the following APIs are available:
    * Create a resource
    * Read a resource given its UUID
    * Update a resource
    * Delete a resource given its UUID
    * List resources of given type

In addition, the following APIs are also available:
    * Listing all resource types
    * Convert FQ name to UUID
    * Convert UUID to FQ name
    * Add/Delete/Update a reference between two objects

Creating a resource
-------------------
To create a resource, a ``POST`` has to be issued on the collection URL.
So for a resource of type *example-resource*,

    * *METHOD*: POST 
    * *URL*: http://<ip>:<port>/example_resources/ 
    * *BODY*: JSON representation of example-resource type
    * *RESPONSE*: UUID and href of created resource

Example request ::

    curl -X POST -H "X-Auth-Token: $OS_TOKEN" -H "Content-Type: application/json; charset=UTF-8" -d '{"virtual-network": {"parent_type": "project", "fq_name": ["default-domain", "admin", "vn-blue"], "network_ipam_refs": [{"attr": {"ipam_subnets": [{"subnet": {"ip_prefix": "10.1.1.0", "ip_prefix_len": 24}}]}, "to": ["default-domain", "default-project", "default-network-ipam"]}]}}' http://10.84.14.2:8082/virtual-networks

Response ::

    {"virtual-network": {"fq_name": ["default-domain", "admin", "vn-blue"], "parent_uuid": "df7649a6-3e2c-4982-b0c3-4b5038eef587", "parent_href": "http://10.84.14.2:8082/project/df7649a6-3e2c-4982-b0c3-4b5038eef587", "uuid": "8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "href": "http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "name": "vn-blue"}}

Reading a resource
-------------------
To read a resource, a ``GET`` has to be issued on the resource URL.

    * *METHOD*: GET
    * *URL*: http://<ip>:<port>/example_resource/<example-resource-uuid>
    * *BODY*: None
    * *RESPONSE*: JSON representation of the resource

Example request ::

    curl -X GET -H "X-Auth-Token: $OS_TOKEN" -H "Content-Type: application/json; charset=UTF-8" http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee

Response ::

    {"virtual-network": {"virtual_network_properties": {"network_id": 4, "vxlan_network_identifier": null, "extend_to_external_routers": null}, "fq_name": ["default-domain", "admin", "vn-blue"], "uuid": "8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "access_control_lists": [{"to": ["default-domain", "admin", "vn-blue", "vn-blue"], "href": "http://10.84.14.2:8082/access-control-list/24b9c337-7be8-4883-a9a0-60197edf64e4", "uuid": "24b9c337-7be8-4883-a9a0-60197edf64e4"}], "network_policy_refs": [{"to": ["default-domain", "admin", "policy-red-blue"], "href": "http://10.84.14.2:8082/network-policy/f215a3ec-5cbd-4310-91f4-7bbca52b27bd", "attr": {"sequence": {"major": 0, "minor": 0}}, "uuid": "f215a3ec-5cbd-4310-91f4-7bbca52b27bd"}], "parent_uuid": "df7649a6-3e2c-4982-b0c3-4b5038eef587", "parent_href": "http://10.84.14.2:8082/project/df7649a6-3e2c-4982-b0c3-4b5038eef587", "parent_type": "project", "href": "http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "id_perms": {"enable": true, "description": null, "created": "2013-09-13T00:26:05.290644", "uuid": {"uuid_mslong": 10125498831222882614, "uuid_lslong": 11086156774262128366}, "last_modified": "2013-09-13T00:47:41.219833", "permissions": {"owner": "cloud-admin", "owner_access": 7, "other_access": 7, "group": "cloud-admin-group", "group_access": 7}}, "routing_instances": [{"to": ["default-domain", "admin", "vn-blue", "vn-blue"], "href": "http://10.84.14.2:8082/routing-instance/732567fd-8607-4045-b6c0-ff4109d3e0fb", "uuid": "732567fd-8607-4045-b6c0-ff4109d3e0fb"}], "network_ipam_refs": [{"to": ["default-domain", "default-project", "default-network-ipam"], "href": "http://10.84.14.2:8082/network-ipam/a01b486e-2c3e-47df-811c-440e59417ed8", "attr": {"ipam_subnets": [{"subnet": {"ip_prefix": "10.1.1.0", "ip_prefix_len": 24}, "default_gateway": "10.1.1.254"}]}, "uuid": "a01b486e-2c3e-47df-811c-440e59417ed8"}], "name": "vn-blue"}}

Updating a resource
--------------------
To update a resource, a ``PUT`` has to be issued on the resource URL.

    * *METHOD*: PUT
    * *URL*: http://<ip>:<port>/example_resource/<example-resource-uuid>
    * *BODY*: JSON representation of resource attributes that are changing
    * *RESPONSE*: UUID and href of updated resource

References to other resources are specified as a list of dictionaries with
"to" and  "attr" keys where "to" is the fully-qualified name of the resource
being referred to and "attr" is the data associated with the relation (if any).

Example request ::

    curl -X PUT -H "X-Auth-Token: $OS_TOKEN" -H "Content-Type: application/json; charset=UTF-8" -d '{"virtual-network": {"fq_name": ["default-domain", "admin", "vn-blue"],"network_policy_refs": [{"to": ["default-domain", "admin", "policy-red-blue"], "attr":{"sequence":{"major":0, "minor": 0}}}]}}' http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee

Response ::

    {"virtual-network": {"href": "http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "uuid": "8c84ff8a-30ac-4136-99d9-f0d9662f3eee"}}

Deleting a resource
-------------------
To delete a resource, a ``DELETE`` has to be issued on the resource URL 

    * *METHOD*: DELETE
    * *URL*: http://<ip>:<port>/example_resource/<example-resource-uuid>
    * *BODY*: None
    * *RESPONSE*: None

Example Request ::

    curl -X DELETE -H "X-Auth-Token: $OS_TOKEN" -H "Content-Type: application/json; charset=UTF-8" http://10.84.14.2:8082/virtual-network/47a91732-629b-4cbe-9aa5-45ba4d7b0e99

Response *None*

Listing Resources
-----------------
To list a set of resources, a ``GET`` has to be issued on the collection URL
with an optional query parameter mentioning the parent resource that contains
this collection. If parent resource is not mentioned, a resource named
'default-<parent-type>' is assumed.

    * *METHOD*: GET
    * *URL*: http://<ip>:<port>/example_resources
             http://<ip>:<port>/example_resources?parent_id=<parent_uuid> *OR*
             http://<ip>:<port>/example_resources?parent_fq_name_str=<parent's fully-qualified name delimited by ':'> *OR*
             http://<ip>:<port>/example_resources?obj_uuids=<example1_uuid>,<example2_uuid>&detail=True *OR*
             http://<ip>:<port>/example_resources?back_ref_id=<back_ref_uuid> *OR*
    * *BODY*: None
    * *RESPONSE*: JSON list of UUID and href of collection if detail not specified, else JSON list of collection dicts


Example request ::

    curl -X GET -H "X-Auth-Token: $OS_TOKEN" -H "Content-Type: application/json; charset=UTF-8" http://10.84.14.2:8082/virtual-networks

Response ::

    {"virtual-networks": [{"href": "http://10.84.14.2:8082/virtual-network/8c84ff8a-30ac-4136-99d9-f0d9662f3eee", "fq_name": ["default-domain", "admin", "vn-blue"], "uuid": "8c84ff8a-30ac-4136-99d9-f0d9662f3eee"}, {"href": "http://10.84.14.2:8082/virtual-network/47a91732-629b-4cbe-9aa5-45ba4d7b0e99", "fq_name": ["default-domain", "admin", "vn-red"], "uuid": "47a91732-629b-4cbe-9aa5-45ba4d7b0e99"}, {"href": "http://10.84.14.2:8082/virtual-network/f423b6c8-deb6-4325-9035-15a8c8bb0a0d", "fq_name": ["default-domain", "default-project", "__link_local__"], "uuid": "f423b6c8-deb6-4325-9035-15a8c8bb0a0d"}, {"href": "http://10.84.14.2:8082/virtual-network/d44a51b0-f2d8-4644-aee0-fe856f970683", "fq_name": ["default-domain", "default-project", "default-virtual-network"], "uuid": "d44a51b0-f2d8-4644-aee0-fe856f970683"}, {"href": "http://10.84.14.2:8082/virtual-network/aad9e80a-8638-449f-a484-5d1bfd58065c", "fq_name": ["default-domain", "default-project", "ip-fabric"], "uuid": "aad9e80a-8638-449f-a484-5d1bfd58065c"}]}

Discovering API server resources
--------------------------------
The resources managed by the server can be be obtained at the root URL(home-page). ::

    curl http://10.84.14.1:8082/ | python -m json.tool

Here is a sample output ::

    {
      "href": "http://10.84.14.2:8082",
      "links": [
        {
          "link": {
            "href": "http://10.84.14.2:8082/documentation/index.html",
            "name": "documentation",
            "rel": "documentation"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/config-root",
            "name": "config-root",
            "rel": "root"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/domains",
            "name": "domain",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/service-instances",
            "name": "service-instance",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/instance-ips",
            "name": "instance-ip",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/network-policys",
            "name": "network-policy",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-DNS-records",
            "name": "virtual-DNS-record",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/route-targets",
            "name": "route-target",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/floating-ips",
            "name": "floating-ip",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/floating-ip-pools",
            "name": "floating-ip-pool",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/bgp-routers",
            "name": "bgp-router",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-routers",
            "name": "virtual-router",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/global-system-configs",
            "name": "global-system-config",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/namespaces",
            "name": "namespace",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/provider-attachments",
            "name": "provider-attachment",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-DNSs",
            "name": "virtual-DNS",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/customer-attachments",
            "name": "customer-attachment",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-machines",
            "name": "virtual-machine",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/service-templates",
            "name": "service-template",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/security-groups",
            "name": "security-group",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/access-control-lists",
            "name": "access-control-list",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/network-ipams",
            "name": "network-ipam",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-networks",
            "name": "virtual-network",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/projects",
            "name": "project",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/routing-instances",
            "name": "routing-instance",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-machine-interfaces",
            "name": "virtual-machine-interface",
            "rel": "collection"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/domain",
            "name": "domain",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/service-instance",
            "name": "service-instance",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/instance-ip",
            "name": "instance-ip",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/network-policy",
            "name": "network-policy",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-DNS-record",
            "name": "virtual-DNS-record",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/route-target",
            "name": "route-target",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/floating-ip",
            "name": "floating-ip",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/floating-ip-pool",
            "name": "floating-ip-pool",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/bgp-router",
            "name": "bgp-router",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-router",
            "name": "virtual-router",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/config-root",
            "name": "config-root",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/global-system-config",
            "name": "global-system-config",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/namespace",
            "name": "namespace",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/provider-attachment",
            "name": "provider-attachment",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-DNS",
            "name": "virtual-DNS",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/customer-attachment",
            "name": "customer-attachment",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-machine",
            "name": "virtual-machine",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/service-template",
            "name": "service-template",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/security-group",
            "name": "security-group",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/access-control-list",
            "name": "access-control-list",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/network-ipam",
            "name": "network-ipam",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-network",
            "name": "virtual-network",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/project",
            "name": "project",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/routing-instance",
            "name": "routing-instance",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-machine-interface",
            "name": "virtual-machine-interface",
            "rel": "resource-base"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/fqname-to-id",
            "name": "name-to-id",
            "rel": "action"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/id-to-fqname",
            "name": "id-to-name",
            "rel": "action"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/useragent-kv",
            "name": "useragent-keyvalue",
            "rel": "action"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-network/%s/ip-alloc",
            "name": "virtual-network-ip-alloc",
            "rel": "action"
          }
        },
        {
          "link": {
            "href": "http://10.84.14.2:8082/virtual-network/%s/ip-free",
            "name": "virtual-network-ip-free",
            "rel": "action"
          }
        }
      ]
    }

Converting FQ name to UUID
--------------------------
To find the UUID of a resource, given its fq name ::

    curl -X POST -H "X-Auth-Token: $OS_TOKEN" -H "Content-Type: application/json; charset=UTF-8" -d '{"fq_name": ["default-domain", "admin", "vn-blue"], "type": "virtual-network"}' http://10.84.14.2:8082/fqname-to-id

Here is a sample output ::

    {"uuid": "e3a20048-8cc7-4cff-8c3b-ada61eb822ed"}
    
Converting UUID to FQ name
--------------------------
To find the type and FQ name of a resource, given its UUID ::

    curl -X POST -H "X-Auth-Token: $OS_TOKEN" -H "Content-Type: application/json; charset=UTF-8" -d '{"uuid": "e3a20048-8cc7-4cff-8c3b-ada61eb822ed"}' http://10.84.14.2:8082/id-to-fqname

Here is a sample output ::

    {"type": "virtual-network", "fq_name": ["default-domain", "admin", "vn-blue"]}
    
Adding/Deleting/Updating a reference between two objects
--------------------------------------------------------

To add/delete/update a reference between two objects, you don't need to read and send the entire object. You can atomically update a single reference by using this API. 
To add or update a reference::

    curl -X POST -H "X-Auth-Token: $OS_TOKEN" -H "Content-Type: application/json; charset=UTF-8" -d '{"operation": "ADD", "uuid": "e3a20048-8cc7-4cff-8c3b-ada61eb822ed", "type": "virtual-network", "ref-type": "network-policy", "ref-uuid": "7810b656-97d9-4c43-94c7-bd52cc4b055d", "attr": {"sequence": {"major": 0, "minor": 0}}}' http://10.84.14.2:8082/ref-update

Note that instead of the ref-uuid, you can also specify ref-fq-name::

    curl -X POST -H "X-Auth-Token: $OS_TOKEN" -H "Content-Type: application/json; charset=UTF-8" -d '{"operation": "ADD", "uuid": "e3a20048-8cc7-4cff-8c3b-ada61eb822ed", "type": "virtual-network", "ref-type": "network-policy", "ref-fq-name": ["default-domain", "default-project", "default-network-policy"], "attr": {"sequence": {"major": 0, "minor": 0}}}' http://10.84.14.2:8082/ref-update

To delete a reference::

    curl -X POST -H "X-Auth-Token: $OS_TOKEN" -H "Content-Type: application/json; charset=UTF-8" -d '{"operation": "DELETE", "uuid": "e3a20048-8cc7-4cff-8c3b-ada61eb822ed", "type": "virtual-network", "ref-type": "network-policy", "ref-uuid": "7810b656-97d9-4c43-94c7-bd52cc4b055d"}' http://10.84.14.2:8082/ref-update


