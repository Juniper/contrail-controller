REST API Details
================
The configuration API server provides a means of accessing and manipulating configuration
elements of the system using HTTP operations on resources represented in JSON.
The REST APIs can be classified into two categories:
   * those that are configuration element type dependant
   * those that are common for all configuration element types

The configuration element types (also referred to as resource types) have a hierarchical relationship 
described in :doc:`vnc_cfg.xsd` schema. The class definitions of all resource types are available at 
:mod:`vnc_cfg_api_server.gen.resource_common` module. JSON representation of these objects are what is 
expected on the wire.

Discovering API server resources
--------------------------------
The resources managed by the server can be be obtained at the root URL(home-page). ::

    [root@a5s1 ~]# curl http://10.84.14.1:8082/
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
            "href": "http://10.84.14.2:8082/ifmap-to-id",
            "name": "ifmap-to-id",
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


Creating a resource
-------------------
To create a resource, a ``POST`` has to be issued on the collection URL.
So for a resource of type *example-resource*,

    * *METHOD*: POST 
    * *URL*: http://<ip>:<port>/example_resources/ 
    * *BODY*: json representation of example-resource type
    * *RESPONSE*: uuid and href of created resource

Reading a resource
-------------------
To read a resource, a ``GET`` has to be issued on the resource URL.

    * *METHOD*: GET
    * *URL*: http://<ip>:<port>/example_resource/<example-resource-uuid>
    * *BODY*: None
    * *RESPONSE*: json representation of the resource

Update a resource
-----------------
To update a resource, a ``PUT`` has to be issued on the resource URL.

    * *METHOD*: PUT
    * *URL*: http://<ip>:<port>/example_resource/<example-resource-uuid>
    * *BODY*: json representation of resource attributes that are changing
    * *RESPONSE*: uuid and href of updated resource

References to other resources are specified as a list of dictionaries with
"to" and  "attr" keys where "to" is the fully-qualified name of the resource
being referred to and "attr" is the data associated with the relation (if any).

Listing Resources
-----------------
To list a set of resources, a ``GET`` has to be issued on the collection URL
with an optional query parameter mentioning the parent resource that contains
this collection. If parent resource is not mentioned, a resource named
'default-<parent-type>' is assumed.

    * *METHOD*: GET
    * *URL*: http://<ip>:<port>/example_resources?parent_id=<parent_uuid> *OR*
           http://<ip>:<port>/example_resources?parent_fq_name_str=<parent's fully-qualified name delimited by ':'>
    * *BODY*: None
    * *RESPONSE*: json list of uuid and href of collection
