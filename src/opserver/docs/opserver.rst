Contrail Analytics API Package
==============================

:mod:`contrail-analytics-api` Module
------------------------------------
contrail-analytics-api provides REST API interface to extract the operational state of
Juniper's Contrail Virtual Network System. These APIs are used by Contrail
Web UI to present operation state to the users. Other applications may use
the Contrail Analytics REST API for analytics and others uses.

The available APIs are provided through the REST interface itself and one
can navigate the URL tree starting at the root (``http://<analytics-api-ip>:<analytics-api-port>``)
to see all of the available APIs.

User Visible Entities
^^^^^^^^^^^^^^^^^^^^^
Contrail Analytics provides API to get the
current state of the User Visible Entities in the Contrail VNS. **User Visible
Entity(UVE)** is defined as an Object that may span multiple components and may
require aggregation before the UVE information is presented. Examples include
``Virtual Network``, ``Virtual Machine`` etc. Operational information of a ``Virtual Network``
may span multiple vRouters, Config Nodes, Control Nodes. Contrail Analytics provides aggregation of
all this information and provides the aggregated information through REST API.
The URL ``/analytics/uves`` shows the list of all UVE types available in the system.

The description of some of the UVEs in the Contrail VNS is given below:

    * ``Virtual Network``: This UVE provides information associated with a virtual network such as:
        - list of networks connected to this network
        - list of virtual machines spawned in this VN
        - list of ACLs associated with this VN
        - global input/output statistics
        - per VN pair input/output statistics

        - The REST api to get all Virtual Network UVEs is through **HTTP GET** using the URL ``/analytics/uves/virtual-networks``
        - The REST api to get a particular Virtual Network UVE is through **HTTP GET** using the URL ``/analytics/uves/virtual-network/<name>``
    * ``Virtual Machine``: This UVE provides information associated with a virtual machine such as:
        - list of interfaces in this virtual machine
        - list of floating IPs associated with each of it's interfaces
        - input/output statistics

        - The REST api to get all Virtual Machine UVEs is through **HTTP GET** using the URL ``/analytics/uves/virtual-machines``
        - The REST api to get a particular Virtual Machine UVE is through **HTTP GET** using the URL ``/analytics/uves/virtual-machine/<name>``
    * ``VRouter``: This UVE provides information associated with a VRouter such as:
        - list of virtual networks present on this VRouter
        - list of virtual machines spawned on the server of this VRouter
        - statistics of the traffic flowing through this VRouter

        - The REST api to get all vRouter UVEs is through **HTTP GET** using the URL ``/analytics/uves/vrouters``
        - The REST api to get a particular vRouter UVE is through **HTTP GET** using the URL ``/analytics/uves/vrouter/<name>``
    * **Contrail Nodes**: There are multiple types of Nodes in Contrail VNS, for e.g Control Node, Config Node, Analytics Node, Compute Node etc.
        There is a UVE for each Node type. The common information associated with each Node UVE is

        - ip address of the Node
        - list of processes, their cpu/memory utilization
        There will also be Node type specific information for e.g

        - Control Node will have information wrt it's connectivity to the vRouter and other Control Nodes
        - Analytics node will have information regarding how many generators are connected to it etc.

        - The REST api to get all Config Node UVEs is through **HTTP GET** using the URL ``/analytics/uves/config-nodes``
        - The REST api to get a particular Config Node UVE is through **HTTP GET** using the URL ``/analytics/uves/config-node/<name>``
        - The APIs are similar for other Node types.
    * **Service Chaining**: There are UVEs related Service Chaining as well viz. Service Chain, Service Instance
        The URLs for these are

        - ``/analytics/uves/service-chains``
        - ``/analytics/uves/service-instances``

**Wild card query of UVEs**: If there is a need to get information for multiple UVEs of the same time, then wildcard query can be issued.
For e.g

- ``/analytics/uves/virtual-network/*`` gives all Virtual Network UVEs
- ``/analytics/uves/virtual-network/project1*`` gives all Virtual Network UVEs that have their name starting with project1.

**Filtered information of UVEs**: As explained earlier UVE information is an aggregated information across all generators. If there is
a need to get partial information of the UVE, then relevant flags can be passed to get only the filtered information.
Supported filtering flags are

- sfilt : for Source (usually hostname of the generator) filtering, 
- mfilt : for Module (module name of the generator) filtering, 
- cfilt : for UVE struct type filtering - useful when UVE is a composition of multiple structs 
For e.g

- ``/analytics/uves/virtual-network/vn1?sfilt=src1`` gives Virtual Network vn1's information provided by all components on Source src1
- ``/analytics/uves/virtual-network/vn1?mfilt=ApiServer`` gives Virtual Network vn1's information provided by all ApiServer modules

Example outputs of the UVEs are given in the below Examples section

Log and Flow Information
^^^^^^^^^^^^^^^^^^^^^^^^
In Contrail VNS, the Log and Flow information is collected and stored centrally
using horizontally scalable Contrail VNS Collector and horizontally scalable
NoSQL database. Contrail Analytics provides REST API to extract this information via
queries. The queries provide well known SQL syntax and hide the underlying
complexity of NoSQL tables.

The following are the **HTTP GET** APIs related to supported queries:
    * ``/analytics/tables``
        this API gives **the list of SQL-type tables** available for querying and the hrefs to get information for each of these tables
    * ``/analytics/table/<table>``
        this API gives for a given table, list of APIs available to get information for this table
    * ``/analytics/table/<table>/schema``
        this API gives schema for a given table

The following is the **HTTP POST** API related to these queries:
    * ``/analytics/query``
        * This API is to extract information from one of the tables. The format of the query follows the SQL syntax of the following form

            | SELECT field1, field2...
            | FROM table1
            | WHERE field1 = value1 AND field3 = value3...
            | FILTER BY...
            | SORT BY...
            | LIMIT 

        In addition to the above, the start time and the end time are mandatory - they define the time period of the query data.

        The parameters of the query are passed through POST data. The information passed has the following fields:
            - start_time: start of the time period
            - end_time: end of the time period
            - table: table from which the data to be extracted
            - select_fields: columns in the final result table
            - where: list of match conditions
            - ...

        POST data is in JSON format and is based on the following idl file.

        .. include:: ../../query_engine/query_rest.idl
           :literal:

        The result of the query API is also in JSON format.

**Query Types**:
Contrail Analytics supports two types of queries - Sync and Async.
POST data parameters as given above are same for both types for queries.
The Client must request an Async query by attaching this header to the POST request: ``Expect: 202-accepted``.
If this header is not present, Contrail Analytics will execute the query synchronously.

**Sync Query**: Contrail Analytics sends the result inline with the query processing

**Async Query**:

``Initiating a Query``: The Client must request an Async query by attaching this header to the POST request: ``Expect: 202-accepted``.

``Examining the status``: In case of an Asynchronous query, the Contrail Analytics will respond with code ``202 Accepted``
The response contents will be an href/URI that represents the status entity for this async query.
(The href will be of the form ``/analytics/query/<QueryID>``. The QueryID will have been assigned by the Contrail Analytics.
The client is expected to poll this status entity (by doing a GET method on it)
The response contents will have a variable named "progress", which will be a number between 0 and 100.
This variable represents "approx. % complete". When "progress" is 100, query processing is complete.

``The "chunk" field of the Status Entity``:
The status entity will also have an element called "chunks", which will contain a list of query result chunks.
Each element of this list will have 3 fields: "start_time", "end_time" and "href".
The Contrail Analytics will decide how many chunks to break up the query into.
If the result of a chunk is not available yet, the chunk's "href" will be an empty string ("").
When the partial result of a chunk is available, the chunk href will be of the form ``/analytics/query/<QueryID>/chunk-partial/<chunk number>``.
When the final result of a chunk is available, the chunk href will be of the form ``/analytics/query/<QueryID>/chunk-final/<chunk number>``.

Example Outputs
^^^^^^^^^^^^^^^

Output to get all UVE types::

    root@a1s33:~# curl -q -u <user>:<password> http://localhost:8181/analytics/uves| python -mjson.tool
    [
        {
            "href": "http://localhost:8181/analytics/uves/storage-pools",
            "name": "storage-pools"
        },  
        {
            "href": "http://localhost:8181/analytics/uves/service-instances",
            "name": "service-instances"
        },  
        {   
            "href": "http://localhost:8181/analytics/uves/servers",
            "name": "servers"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/storage-disks",
            "name": "storage-disks"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/service-chains",
            "name": "service-chains"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/generators",
            "name": "generators"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/bgp-peers",
            "name": "bgp-peers"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/physical-interfaces",
            "name": "physical-interfaces"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/xmpp-peers",
            "name": "xmpp-peers"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/storage-clusters",
            "name": "storage-clusters"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/analytics-nodes",
            "name": "analytics-nodes"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/config-nodes",
            "name": "config-nodes"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/virtual-machines",
            "name": "virtual-machines"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/control-nodes",
            "name": "control-nodes"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/prouters",
            "name": "prouters"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/database-nodes",
            "name": "database-nodes"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/virtual-machine-interfaces",
            "name": "virtual-machine-interfaces"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/virtual-networks",
            "name": "virtual-networks"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/logical-interfaces",
            "name": "logical-interfaces"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/loadbalancers",
            "name": "loadbalancers"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/vrouters",
            "name": "vrouters"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/storage-osds",
            "name": "storage-osds"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/routing-instances",
            "name": "routing-instances"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/user-defined-log-statistics",
            "name": "user-defined-log-statistics"
        },
        {   
            "href": "http://localhost:8181/analytics/uves/dns-nodes",
            "name": "dns-nodes"
        }
    ]
    
Output to get all virtual network UVEs::

    root@a1s33:~# curl -u <user>:<password> localhost:8181/analytics/uves/virtual-networks  | python -mjson.tool
    [
        {
            "href": "http://localhost:8181/analytics/uves/virtual-network/default-domain:default-project:__link_local__?flat",
            "name": "default-domain:default-project:__link_local__"
        },
        {
            "href": "http://localhost:8181/analytics/uves/virtual-network/default-domain:default-project:ip-fabric?flat",
            "name": "default-domain:default-project:ip-fabric"
        },
        {
            "href": "http://localhost:8181/analytics/uves/virtual-network/default-domain:default-project:default-virtual-network?flat",
            "name": "default-domain:default-project:default-virtual-network"
        },
        {
            "href": "http://localhost:8181/analytics/uves/virtual-network/default-domain:admin:vn1?flat",
            "name": "default-domain:admin:vn1"
        },
        {
            "href": "http://localhost:8181/analytics/uves/virtual-network/default-domain:admin:vn2?flat",
            "name": "default-domain:admin:vn2"
        },
        {
            "href": "http://localhost:8181/analytics/uves/virtual-network/__UNKNOWN__?flat",
            "name": "__UNKNOWN__"
        }
    ]
    
Output to get a virtual network UVE::
    
    root@a1s33:~# curl -u <user>:<password> http://localhost:8181/analytics/uves/virtual-network/default-domain:admin:vn1?flat  | python -mjson.tool
    {
        "ContrailConfig": {
            "deleted": false,
            "elements": {
                "display_name": "\"vn1\"",
                "ecmp_hashing_include_fields": "{}",
                "export_route_target_list": "{\"route_target\": []}",
                "flood_unknown_unicast": "false",
                "fq_name": "[\"default-domain\", \"admin\", \"vn1\"]",
                "id_perms": "{\"enable\": true, \"uuid\": {\"uuid_mslong\": 10825428566007893130, \"uuid_lslong\": 13151146520657792148}, \"creator\": null, \"created\": \"2017-03-29T18:22:21.064248\", \"user_visible\": true, \"last_modified\": \"2017-03-29T18:23:40.965224\", \"permissions\": {\"owner\": \"neutron\", \"owner_access\": 7, \"other_access\": 7, \"group\": \"_member_\", \"group_access\": 7}, \"description\": null}",
                "import_route_target_list": "{\"route_target\": []}",
                "is_shared": "false",
                "multi_policy_service_chains_enabled": "false",
                "network_ipam_refs": "[{\"to\": [\"default-domain\", \"default-project\", \"default-network-ipam\"], \"href\": \"http://0.0.0.0:9100/network-ipam/8874025b-0f32-4c75-85d9-d56c87fc69d5\", \"attr\": {\"ipam_subnets\": [{\"subnet\": {\"ip_prefix\": \"1.1.1.0\", \"ip_prefix_len\": 24}, \"addr_from_start\": true, \"enable_dhcp\": true, \"default_gateway\": \"1.1.1.1\", \"dns_nameservers\": [], \"subnet_uuid\": \"df9357bf-d5bd-48f9-980f-e4e796a60164\", \"alloc_unit\": 1, \"subnet_name\": \"\", \"dns_server_address\": \"1.1.1.2\"}]}, \"uuid\": \"8874025b-0f32-4c75-85d9-d56c87fc69d5\"}]",
                "network_policy_refs": "[{\"to\": [\"default-domain\", \"admin\", \"vn1tovn2\"], \"href\": \"http://0.0.0.0:9100/network-policy/2a9ea8df-83ee-482d-9354-e5368de499a6\", \"attr\": {\"timer\": null, \"sequence\": {\"major\": 0, \"minor\": 0}}, \"uuid\": \"2a9ea8df-83ee-482d-9354-e5368de499a6\"}]",
                "parent_href": "\"http://0.0.0.0:9100/project/f5eb93b4-28f9-49ce-83dd-ff7ee0685898\"",
                "parent_type": "\"project\"",
                "parent_uuid": "\"f5eb93b4-28f9-49ce-83dd-ff7ee0685898\"",
                "perms2": "{\"owner\": \"e37d1389a9f34bf5a8f08c95677ab0c2\", \"owner_access\": 7, \"global_access\": 0, \"share\": []}",
                "port_security_enabled": "true",
                "provider_properties": "null",
                "route_target_list": "{\"route_target\": []}",
                "router_external": "false",
                "routing_instances": "[{\"to\": [\"default-domain\", \"admin\", \"vn1\", \"vn1\"], \"href\": \"http://0.0.0.0:9100/routing-instance/d071b1cf-d495-4d57-a1e5-4743b06d0196\", \"uuid\": \"d071b1cf-d495-4d57-a1e5-4743b06d0196\"}]",
                "uuid": "\"963ba5ec-da44-4c8a-b682-421530ec0c94\"",
                "virtual_network_network_id": "4",
                "virtual_network_properties": "{\"allow_transit\": false, \"mirror_destination\": false, \"rpf\": \"enable\"}"
            }
        },
        "UveVirtualNetworkConfig": {
            "connected_networks": [
                "default-domain:admin:vn2"
            ],
            "routing_instance_list": [
                "default-domain:admin:vn1:vn1"
            ],
            "total_acl_rules": 4
        }
    }
    
Output to get all vrouter UVEs::
    
    root@a1s33:~# curl -u <user>:<password> http://localhost:8181/analytics/uves/vrouters| python -mjson.tool
    [
        {
            "href": "http://localhost:8181/analytics/uves/vrouter/a1s34?flat",
            "name": "a1s34"
        }
    ]
    
    
Output to get a vrouter UVE::
    
    root@a1s33:~# 
    root@a1s33:~# curl -u <user>:<password> http://localhost:8181/analytics/uves/vrouter/a1s34?flat| python -mjson.tool
    {
        "ComputeCpuState": {
            "cpu_info": [
                {
                    "cpu_share": 0.63125,
                    "mem_res": 218848,
                    "mem_virt": 1058592,
                    "one_min_cpuload": 0.0,
                    "used_sys_mem": 1424196
                }
            ]
        },
        "ContrailConfig": {
            "deleted": false,
            "elements": {
                "display_name": "\"a1s34\"",
                "fq_name": "[\"default-global-system-config\", \"a1s34\"]",
                "id_perms": "{\"enable\": true, \"uuid\": {\"uuid_mslong\": 18279099490370276764, \"uuid_lslong\": 12751836577221776082}, \"created\": \"2017-03-22T21:18:06.821767\", \"description\": null, \"creator\": null, \"user_visible\": true, \"last_modified\": \"2017-03-22T21:18:06.821767\", \"permissions\": {\"owner\": \"admin\", \"owner_access\": 7, \"other_access\": 7, \"group\": \"admin\", \"group_access\": 7}}",
                "parent_href": "\"http://0.0.0.0:9100/global-system-config/44b23153-7aa7-40eb-bb2d-c67915df9502\"",
                "parent_type": "\"global-system-config\"",
                "parent_uuid": "\"44b23153-7aa7-40eb-bb2d-c67915df9502\"",
                "perms2": "{\"owner\": \"f5eb93b428f949ce83ddff7ee0685898\", \"owner_access\": 7, \"global_access\": 0, \"share\": []}",
                "uuid": "\"fdac6823-c18b-4d9c-b0f7-9fce0b2312d2\"",
                "virtual_router_dpdk_enabled": "false",
                "virtual_router_ip_address": "\"10.84.5.34\"",
                "virtual_router_type": "[]"
            }
        },
        "NodeStatus": {
            "deleted": false,
            "disk_usage_info": {
                "/dev/mapper/a1s34--vg-root": {
                    "partition_space_available_1k": 874767564,
                    "partition_space_used_1k": 3365340,
                    "partition_type": "ext4",
                    "percentage_partition_space_used": 0
                },
                "/dev/sda1": {
                    "partition_space_available_1k": 192010,
                    "partition_space_used_1k": 36521,
                    "partition_type": "ext2",
                    "percentage_partition_space_used": 16
                }
            },
            "process_info": [
                {
                    "core_file_list": [],
                    "exit_count": 0,
                    "last_exit_time": null,
                    "last_start_time": "1490306670121712",
                    "last_stop_time": null,
                    "process_name": "contrail-vrouter-agent",
                    "process_state": "PROCESS_STATE_RUNNING",
                    "start_count": 1,
                    "stop_count": 0
                },
                {
                    "core_file_list": [],
                    "exit_count": 0,
                    "last_exit_time": null,
                    "last_start_time": "1490306666200275",
                    "last_stop_time": null,
                    "process_name": "contrail-vrouter-nodemgr",
                    "process_state": "PROCESS_STATE_RUNNING",
                    "start_count": 1,
                    "stop_count": 0
                }
            ],
            "process_mem_cpu_usage": {
                "contrail-vrouter-agent": {
                    "cpu_share": 0.63,
                    "mem_res": 218848,
                    "mem_virt": 1058592
                },
                "contrail-vrouter-nodemgr": {
                    "cpu_share": 0.0,
                    "mem_res": 29384,
                    "mem_virt": 169856
                }
            },
            "process_status": [
                {
                    "connection_infos": [
                        {
                            "description": "ClientInit to Established on EvSandeshCtrlMessageRecv",
                            "name": null,
                            "server_addrs": [
                                "10.84.5.33:8086"
                            ],
                            "status": "Up",
                            "type": "Collector"
                        }
                    ],
                    "description": null,
                    "instance_id": "0",
                    "module_id": "contrail-vrouter-nodemgr",
                    "state": "Functional"
                },
                {
                    "connection_infos": [
                        {
                            "description": "OpenSent",
                            "name": "control-node:10.84.5.33",
                            "server_addrs": [
                                "10.84.5.33:5269"
                            ],
                            "status": "Up",
                            "type": "XMPP"
                        },
                        {
                            "description": "OpenSent",
                            "name": "dns-server:10.84.5.33",
                            "server_addrs": [
                                "10.84.5.33:53"
                            ],
                            "status": "Up",
                            "type": "XMPP"
                        },
                        {
                            "description": "Established",
                            "name": null,
                            "server_addrs": [
                                "10.84.5.33:8086"
                            ],
                            "status": "Up",
                            "type": "Collector"
                        },
                        {
                            "description": "SubscribeResponse",
                            "name": "dns-server",
                            "server_addrs": [
                                "10.84.5.33:5998"
                            ],
                            "status": "Up",
                            "type": "Discovery"
                        },
                        {
                            "description": "SubscribeResponse",
                            "name": "xmpp-server",
                            "server_addrs": [
                                "10.84.5.33:5998"
                            ],
                            "status": "Up",
                            "type": "Discovery"
                        }
                    ],
                    "description": null,
                    "instance_id": "0",
                    "module_id": "contrail-vrouter-agent",
                    "state": "Functional"
                }
            ],
            "system_cpu_info": {
                "num_core_per_socket": 6,
                "num_cpu": 24,
                "num_socket": 2,
                "num_thread_per_core": 2
            },
            "system_cpu_usage": {
                "cpu_share": 0.03,
                "fifteen_min_avg": 0.05,
                "five_min_avg": 0.01,
                "node_type": "vrouter",
                "one_min_avg": 0.0
            },
            "system_mem_usage": {
                "buffers": 169700,
                "cached": 199156,
                "free": 129947988,
                "node_type": "vrouter",
                "total": 131742028,
                "used": 1794040
            }
        },
        "VrouterAgent": {
            "build_info": "{\"build-info\":[{\"build-time\":\"2017-03-21 01:47:18.442258\",\"build-hostname\":\"contrail-ec-build09\",\"build-user\":\"contrail-builder\",\"build-version\":\"3.1.2.0\",\"build-id\":\"3.1.2.0-68\",\"build-number\":\"68\"}]}",
            "collector_server_list_cfg": [
                "10.84.5.33:8086"
            ],
            "config_file": "/etc/contrail/contrail-vrouter-agent.conf",
            "control_ip": "10.84.5.34",
            "control_node_list_cfg": [
                "0.0.0.0",
                "0.0.0.0"
            ],
            "dns_server_list_cfg": [
                "0.0.0.0",
                "0.0.0.0"
            ],
            "dns_servers": [
                "10.84.5.33"
            ],
            "ds_addr": "10.84.5.33",
            "ds_xs_instances": 1,
            "eth_name": "eth0",
            "flow_cache_timeout_cfg": 0,
            "headless_mode_cfg": false,
            "hostname_cfg": "a1s34",
            "hypervisor": "kvm",
            "ll_max_system_flows_cfg": 2048,
            "ll_max_vm_flows_cfg": 2048,
            "log_category": null,
            "log_file": "/var/log/contrail/contrail-vrouter-agent.log",
            "log_flow": false,
            "log_level": "SYS_NOTICE",
            "log_local": true,
            "max_vm_flows_cfg": 100,
            "mode": "VROUTER",
            "phy_if": [
                {
                    "mac_address": "00:25:90:93:d1:ce",
                    "name": "eth0"
                }
            ],
            "platform": "HOST",
            "sandesh_http_port": 8085,
            "self_ip_list": [
                "10.84.5.34"
            ],
            "tunnel_type": "MPLSoGRE",
            "vhost_cfg": {
                "gateway": "10.84.5.254",
                "ip": "10.84.5.34",
                "ip_prefix_len": 24,
                "name": "vhost0"
            },
            "vhost_if": {
                "mac_address": "00:25:90:93:d1:ce",
                "name": "vhost0"
            },
            "vr_limits": {
                "max_interfaces": 4352,
                "max_labels": 5120,
                "max_mirror_entries": 255,
                "max_nexthops": 65536,
                "max_vrfs": 4096,
                "vrouter_build_info": "{\"build-info\": [{\"build-time\": \"Wed Mar 22 13:53:22 PDT 2017\",\"build-hostname\": \"a1s34\", \"build-git-ver\": \"dkms\",\"build-user\": \"root\", \"build-version\": \"3.1.2.0-dkms\"}]}",
                "vrouter_max_bridge_entries": 262144,
                "vrouter_max_flow_entries": 524288,
                "vrouter_max_oflow_bridge_entries": 53248,
                "vrouter_max_oflow_entries": 105472
            },
            "xmpp_peer_list": [
                {
                    "ip": "10.84.5.33",
                    "primary": true,
                    "setup_time": 1490306665057644,
                    "status": true
                }
            ]
        },
        "VrouterStatsAgent": {
            "active_flows_ewm": {
                "algo": "EWM",
                "config": "0.1",
                "samples": 16848,
                "sigma": 0.0,
                "state": {
                    "mean": "0",
                    "stddev": "0"
                }
            },
            "added_flows_ewm": {
                "algo": "EWM",
                "config": "0.1",
                "samples": 16848,
                "sigma": 0.0,
                "state": {
                    "mean": "0",
                    "stddev": "0"
                }
            },
            "aged_flows": 0,
            "cpu_info": {
                "cpu_share": 0.63125,
                "cpuload": {
                    "fifteen_min_avg": 0.00208333,
                    "five_min_avg": 0.000416667,
                    "one_min_avg": 0.0
                },
                "meminfo": {
                    "peakvirt": 1124128,
                    "res": 218848,
                    "virt": 1058592
                },
                "num_cpu": 24,
                "sys_mem_info": {
                    "buffers": 169700,
                    "cached": 199160,
                    "free": 129948972,
                    "node_type": null,
                    "total": 131742028,
                    "used": 1793056
                }
            },
            "deleted_flows_ewm": {
                "algo": "EWM",
                "config": "0.1",
                "samples": 16848,
                "sigma": 0.0,
                "state": {
                    "mean": "0",
                    "stddev": "0"
                }
            },
            "drop_stats": {
                "ds_arp_no_route": 0,
                "ds_arp_no_where_to_go": 0,
                "ds_cksum_err": 0,
                "ds_clone_fail": 0,
                "ds_discard": 0,
                "ds_drop_new_flow": 0,
                "ds_drop_pkts": 10,
                "ds_duplicated": 0,
                "ds_flood": 0,
                "ds_flow_action_drop": 0,
                "ds_flow_action_invalid": 0,
                "ds_flow_evict": 0,
                "ds_flow_invalid_protocol": 0,
                "ds_flow_nat_no_rflow": 0,
                "ds_flow_no_memory": 0,
                "ds_flow_queue_limit_exceeded": 0,
                "ds_flow_table_full": 0,
                "ds_flow_unusable": 0,
                "ds_frag_err": 0,
                "ds_fragment_queue_fail": 0,
                "ds_garp_from_vm": 0,
                "ds_head_alloc_fail": 0,
                "ds_head_space_reserve_fail": 0,
                "ds_interface_drop": 0,
                "ds_interface_rx_discard": 0,
                "ds_interface_tx_discard": 0,
                "ds_invalid_arp": 0,
                "ds_invalid_if": 0,
                "ds_invalid_label": 0,
                "ds_invalid_mcast_source": 0,
                "ds_invalid_nh": 10,
                "ds_invalid_packet": 0,
                "ds_invalid_protocol": 0,
                "ds_invalid_source": 0,
                "ds_invalid_vnid": 0,
                "ds_l2_no_route": 0,
                "ds_mcast_clone_fail": 0,
                "ds_mcast_df_bit": 0,
                "ds_misc": 0,
                "ds_no_fmd": 0,
                "ds_no_memory": 0,
                "ds_nowhere_to_go": 0,
                "ds_pcow_fail": 0,
                "ds_pull": 0,
                "ds_push": 0,
                "ds_rewrite_fail": 0,
                "ds_trap_no_if": 0,
                "ds_trap_original": 0,
                "ds_ttl_exceeded": 0,
                "ds_vlan_fwd_enq": 0,
                "ds_vlan_fwd_tx": 0
            },
            "drop_stats_1h": {
                "ds_arp_no_route": 0,
                "ds_arp_no_where_to_go": 0,
                "ds_cksum_err": 0,
                "ds_clone_fail": 0,
                "ds_discard": 0,
                "ds_drop_new_flow": 0,
                "ds_drop_pkts": 0,
                "ds_duplicated": 0,
                "ds_flood": 0,
                "ds_flow_action_drop": 0,
                "ds_flow_action_invalid": 0,
                "ds_flow_evict": 0,
                "ds_flow_invalid_protocol": 0,
                "ds_flow_nat_no_rflow": 0,
                "ds_flow_no_memory": 0,
                "ds_flow_queue_limit_exceeded": 0,
                "ds_flow_table_full": 0,
                "ds_flow_unusable": 0,
                "ds_frag_err": 0,
                "ds_fragment_queue_fail": 0,
                "ds_garp_from_vm": 0,
                "ds_head_alloc_fail": 0,
                "ds_head_space_reserve_fail": 0,
                "ds_interface_drop": 0,
                "ds_interface_rx_discard": 0,
                "ds_interface_tx_discard": 0,
                "ds_invalid_arp": 0,
                "ds_invalid_if": 0,
                "ds_invalid_label": 0,
                "ds_invalid_mcast_source": 0,
                "ds_invalid_nh": 0,
                "ds_invalid_packet": 0,
                "ds_invalid_protocol": 0,
                "ds_invalid_source": 0,
                "ds_invalid_vnid": 0,
                "ds_l2_no_route": 0,
                "ds_mcast_clone_fail": 0,
                "ds_mcast_df_bit": 0,
                "ds_misc": 0,
                "ds_no_fmd": 0,
                "ds_no_memory": 0,
                "ds_nowhere_to_go": 0,
                "ds_pcow_fail": 0,
                "ds_pull": 0,
                "ds_push": 0,
                "ds_rewrite_fail": 0,
                "ds_trap_no_if": 0,
                "ds_trap_original": 0,
                "ds_ttl_exceeded": 0,
                "ds_vlan_fwd_enq": 0,
                "ds_vlan_fwd_tx": 0
            },
            "exception_packets": 8087,
            "exception_packets_allowed": 8079,
            "exception_packets_dropped": 8,
            "flow_export_disable_drops": 0,
            "flow_export_drops": 0,
            "flow_export_sampling_drops": 0,
            "flow_rate": {
                "active_flows": 0,
                "added_flows": 0,
                "deleted_flows": 0,
                "max_flow_adds_per_second": 0,
                "max_flow_deletes_per_second": 0,
                "min_flow_adds_per_second": 0,
                "min_flow_deletes_per_second": 0
            },
            "ifmap_stats_1h": {
                "link_delete_parse_errors": "0",
                "link_update_parse_errors": "0",
                "node_delete_parse_errors": "0",
                "node_update_parse_errors": "0"
            },
            "in_bps_ewm": {
                "eth0": {
                    "algo": "EWM",
                    "config": "0.1",
                    "samples": 8358,
                    "sigma": -0.258216,
                    "state": {
                        "mean": "5842.13",
                        "stddev": "232.851"
                    }
                }
            },
            "in_bytes": 0,
            "in_pkts_ewm": {
                "eth0": {
                    "algo": "EWM",
                    "config": "0.2",
                    "samples": 16848,
                    "sigma": 0.874961,
                    "state": {
                        "mean": "122.585",
                        "stddev": "23.3328"
                    }
                }
            },
            "in_tpkts": 0,
            "out_bps_ewm": {
                "eth0": {
                    "algo": "EWM",
                    "config": "0.1",
                    "samples": 8358,
                    "sigma": -0.605347,
                    "state": {
                        "mean": "14594.7",
                        "stddev": "468.629"
                    }
                }
            },
            "out_bytes": 0,
            "out_pkts_ewm": {
                "eth0": {
                    "algo": "EWM",
                    "config": "0.2",
                    "samples": 16848,
                    "sigma": 0.0697797,
                    "state": {
                        "mean": "101.692",
                        "stddev": "18.7493"
                    }
                }
            },
            "out_tpkts": 0,
            "phy_active_flows_ewm": {
                "eth0": {
                    "algo": "EWM",
                    "config": "0.2",
                    "samples": 16848,
                    "sigma": 0.0,
                    "state": {
                        "mean": "0",
                        "stddev": "0"
                    }
                }
            },
            "phy_added_flows_ewm": {
                "eth0": {
                    "algo": "EWM",
                    "config": "0.2",
                    "samples": 16848,
                    "sigma": 0.0,
                    "state": {
                        "mean": "0",
                        "stddev": "0"
                    }
                }
            },
            "phy_band_in_bps": {
                "eth0": "5782"
            },
            "phy_band_out_bps": {
                "eth0": "14311"
            },
            "phy_deleted_flows_ewm": {
                "eth0": {
                    "algo": "EWM",
                    "config": "0.2",
                    "samples": 16848,
                    "sigma": 0.0,
                    "state": {
                        "mean": "0",
                        "stddev": "0"
                    }
                }
            },
            "phy_flow_rate": {
                "eth0": {
                    "active_flows": 0,
                    "added_flows": 0,
                    "deleted_flows": 0,
                    "max_flow_adds_per_second": 0,
                    "max_flow_deletes_per_second": 0,
                    "min_flow_adds_per_second": 0,
                    "min_flow_deletes_per_second": 0
                }
            },
            "phy_if_5min_usage": [
                {
                    "in_bandwidth_usage": 5853,
                    "name": "eth0",
                    "out_bandwidth_usage": 14634
                }
            ],
            "phy_if_stats": {
                "eth0": {
                    "in_bytes": 13132,
                    "in_pkts": 143,
                    "out_bytes": 48450,
                    "out_pkts": 103
                }
            },
            "total_flows": 0,
            "total_in_bandwidth_utilization": 0.000551414,
            "total_out_bandwidth_utilization": 0.0013648,
            "uptime": 1490306664730083,
            "vhost_stats": {
                "duplexity": -1,
                "in_bytes": 924549402,
                "in_pkts": 1790956,
                "name": "vhost0",
                "out_bytes": 368437829,
                "out_pkts": 2060427,
                "speed": -1
            },
            "xmpp_stats_list": [
                {
                    "in_msgs": 0,
                    "ip": "10.84.5.33",
                    "out_msgs": 2,
                    "reconnects": 1
                }
            ]
        }
    }
    
List of all tables supported in Analytics::
    
    root@a1s33:~# curl -u <user>:<password> http://localhost:8181/analytics/tables| python -mjson.tool
    [
        {
            "href": "http://localhost:8181/analytics/table/MessageTable",
            "name": "MessageTable",
            "type": "LOG"
        },
        {
            "href": "http://localhost:8181/analytics/table/FlowRecordTable",
            "name": "FlowRecordTable",
            "type": "FLOW"
        },
        {
            "href": "http://localhost:8181/analytics/table/FlowSeriesTable",
            "name": "FlowSeriesTable",
            "type": "FLOW"
        },
        {
            "href": "http://localhost:8181/analytics/table/OverlayToUnderlayFlowMap",
            "name": "OverlayToUnderlayFlowMap",
            "type": "FLOW"
        },
        {
            "display_name": "Service Chain",
            "href": "http://localhost:8181/analytics/table/ServiceChain",
            "name": "ServiceChain",
            "type": "OBJECT"
        },
        {
            "display_name": "Database Node",
            "href": "http://localhost:8181/analytics/table/ObjectDatabaseInfo",
            "name": "ObjectDatabaseInfo",
            "type": "OBJECT"
        },
        {
            "display_name": "Routing Instance",
            "href": "http://localhost:8181/analytics/table/ObjectRoutingInstance",
            "name": "ObjectRoutingInstance",
            "type": "OBJECT"
        },
        {
            "display_name": "XMPP Connection",
            "href": "http://localhost:8181/analytics/table/ObjectXmppConnection",
            "name": "ObjectXmppConnection",
            "type": "OBJECT"
        },
        {
            "display_name": "Query Object Table",
            "href": "http://localhost:8181/analytics/table/ObjectQueryTable",
            "name": "ObjectQueryTable",
            "type": "OBJECT"
        },
        {
            "display_name": "Virtual Machine Interface",
            "href": "http://localhost:8181/analytics/table/ObjectVMITable",
            "name": "ObjectVMITable",
            "type": "OBJECT"
        },
        {
            "display_name": "Config Object by User Table",
            "href": "http://localhost:8181/analytics/table/ConfigObjectTableByUser",
            "name": "ConfigObjectTableByUser",
            "type": "OBJECT"
        },
        {
            "display_name": "Query Object Qid",
            "href": "http://localhost:8181/analytics/table/ObjectQueryQid",
            "name": "ObjectQueryQid",
            "type": "OBJECT"
        },
        {
            "display_name": "Storage Device",
            "href": "http://localhost:8181/analytics/table/ObjectOsdTable",
            "name": "ObjectOsdTable",
            "type": "OBJECT"
        },
        {
            "display_name": "Logical Interface",
            "href": "http://localhost:8181/analytics/table/ObjectLogicalInterfaceTable",
            "name": "ObjectLogicalInterfaceTable",
            "type": "OBJECT"
        },
        {
            "display_name": "XMPP Peer",
            "href": "http://localhost:8181/analytics/table/ObjectXmppPeerInfo",
            "name": "ObjectXmppPeerInfo",
            "type": "OBJECT"
        },
        {
            "display_name": "Generator",
            "href": "http://localhost:8181/analytics/table/ObjectGeneratorInfo",
            "name": "ObjectGeneratorInfo",
            "type": "OBJECT"
        },
        {
            "display_name": "Virtual Network",
            "href": "http://localhost:8181/analytics/table/ObjectVNTable",
            "name": "ObjectVNTable",
            "type": "OBJECT"
        },
        {
            "display_name": "Analytics Node",
            "href": "http://localhost:8181/analytics/table/ObjectCollectorInfo",
            "name": "ObjectCollectorInfo",
            "type": "OBJECT"
        },
        {
            "display_name": "pRouter",
            "href": "http://localhost:8181/analytics/table/ObjectPRouter",
            "name": "ObjectPRouter",
            "type": "OBJECT"
        },
        {
            "display_name": "BGP Peer",
            "href": "http://localhost:8181/analytics/table/ObjectBgpPeer",
            "name": "ObjectBgpPeer",
            "type": "OBJECT"
        },
        {
            "display_name": "Loadbalancer",
            "href": "http://localhost:8181/analytics/table/ObjectLBTable",
            "name": "ObjectLBTable",
            "type": "OBJECT"
        },
        {
            "display_name": "User Defined Log Statistic",
            "href": "http://localhost:8181/analytics/table/UserDefinedLogStatTable",
            "name": "UserDefinedLogStatTable",
            "type": "OBJECT"
        },
        {
            "display_name": "Config Object Table",
            "href": "http://localhost:8181/analytics/table/ConfigObjectTable",
            "name": "ConfigObjectTable",
            "type": "OBJECT"
        },
        {
            "display_name": "DNS Node",
            "href": "http://localhost:8181/analytics/table/ObjectDns",
            "name": "ObjectDns",
            "type": "OBJECT"
        },
        {
            "display_name": "Storage Cluster",
            "href": "http://localhost:8181/analytics/table/ObjectStorageClusterTable",
            "name": "ObjectStorageClusterTable",
            "type": "OBJECT"
        },
        {
            "display_name": "Control Node",
            "href": "http://localhost:8181/analytics/table/ObjectBgpRouter",
            "name": "ObjectBgpRouter",
            "type": "OBJECT"
        },
        {
            "display_name": "Physical Interface",
            "href": "http://localhost:8181/analytics/table/ObjectPhysicalInterfaceTable",
            "name": "ObjectPhysicalInterfaceTable",
            "type": "OBJECT"
        },
        {
            "display_name": "Server Table Info",
            "href": "http://localhost:8181/analytics/table/ObjectServerTable",
            "name": "ObjectServerTable",
            "type": "OBJECT"
        },
        {
            "display_name": "Virtual Machine",
            "href": "http://localhost:8181/analytics/table/ObjectVMTable",
            "name": "ObjectVMTable",
            "type": "OBJECT"
        },
        {
            "display_name": "vRouter",
            "href": "http://localhost:8181/analytics/table/ObjectVRouter",
            "name": "ObjectVRouter",
            "type": "OBJECT"
        },
        {
            "display_name": "Storage RawDisk",
            "href": "http://localhost:8181/analytics/table/ObjectDiskTable",
            "name": "ObjectDiskTable",
            "type": "OBJECT"
        },
        {
            "display_name": "Storage Pool",
            "href": "http://localhost:8181/analytics/table/ObjectPoolTable",
            "name": "ObjectPoolTable",
            "type": "OBJECT"
        },
        {
            "display_name": "Service Instance",
            "href": "http://localhost:8181/analytics/table/ObjectSITable",
            "name": "ObjectSITable",
            "type": "OBJECT"
        },
        {
            "display_name": "Config Node",
            "href": "http://localhost:8181/analytics/table/ObjectConfigNode",
            "name": "ObjectConfigNode",
            "type": "OBJECT"
        },
        {
            "display_name": "Analytics CPU Information",
            "href": "http://localhost:8181/analytics/table/StatTable.AnalyticsCpuState.cpu_info",
            "name": "StatTable.AnalyticsCpuState.cpu_info",
            "type": "STAT"
        },
        {
            "display_name": "Config CPU Information",
            "href": "http://localhost:8181/analytics/table/StatTable.ConfigCpuState.cpu_info",
            "name": "StatTable.ConfigCpuState.cpu_info",
            "type": "STAT"
        },
        {
            "display_name": "Control CPU Information",
            "href": "http://localhost:8181/analytics/table/StatTable.ControlCpuState.cpu_info",
            "name": "StatTable.ControlCpuState.cpu_info",
            "type": "STAT"
        },
        {
            "display_name": "Physical Router Interface Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.PRouterEntry.ifStats",
            "name": "StatTable.PRouterEntry.ifStats",
            "type": "STAT"
        },
        {
            "display_name": "Compute CPU Information",
            "href": "http://localhost:8181/analytics/table/StatTable.ComputeCpuState.cpu_info",
            "name": "StatTable.ComputeCpuState.cpu_info",
            "type": "STAT"
        },
        {
            "display_name": "VM CPU Stats",
            "href": "http://localhost:8181/analytics/table/StatTable.VirtualMachineStats.cpu_stats",
            "name": "StatTable.VirtualMachineStats.cpu_stats",
            "type": "STAT"
        },
        {
            "display_name": "Storage Cluster Info",
            "href": "http://localhost:8181/analytics/table/StatTable.StorageCluster.info_stats",
            "name": "StatTable.StorageCluster.info_stats",
            "type": "STAT"
        },
        {
            "display_name": "Storage Pool Info",
            "href": "http://localhost:8181/analytics/table/StatTable.ComputeStoragePool.info_stats",
            "name": "StatTable.ComputeStoragePool.info_stats",
            "type": "STAT"
        },
        {
            "display_name": "Storage Device Info",
            "href": "http://localhost:8181/analytics/table/StatTable.ComputeStorageOsd.info_stats",
            "name": "StatTable.ComputeStorageOsd.info_stats",
            "type": "STAT"
        },
        {
            "display_name": "Storage Raw Device Info",
            "href": "http://localhost:8181/analytics/table/StatTable.ComputeStorageDisk.info_stats",
            "name": "StatTable.ComputeStorageDisk.info_stats",
            "type": "STAT"
        },
        {
            "display_name": "Server Monitoring Sensor Stats Info",
            "href": "http://localhost:8181/analytics/table/StatTable.ServerMonitoringInfo.sensor_stats",
            "name": "StatTable.ServerMonitoringInfo.sensor_stats",
            "type": "STAT"
        },
        {
            "display_name": "Server Monitoring Disk Stats Info",
            "href": "http://localhost:8181/analytics/table/StatTable.ServerMonitoringInfo.disk_usage_stats",
            "name": "StatTable.ServerMonitoringInfo.disk_usage_stats",
            "type": "STAT"
        },
        {
            "display_name": "Server Monitoring Interface Stats Info",
            "href": "http://localhost:8181/analytics/table/StatTable.ServerMonitoringSummary.network_info_stats",
            "name": "StatTable.ServerMonitoringSummary.network_info_stats",
            "type": "STAT"
        },
        {
            "display_name": "Server Monitoring Resource Stats Info",
            "href": "http://localhost:8181/analytics/table/StatTable.ServerMonitoringSummary.resource_info_stats",
            "name": "StatTable.ServerMonitoringSummary.resource_info_stats",
            "type": "STAT"
        },
        {
            "display_name": "Server Monitoring File System Stats Info",
            "href": "http://localhost:8181/analytics/table/StatTable.ServerMonitoringInfo.file_system_view_stats.physical_disks",
            "name": "StatTable.ServerMonitoringInfo.file_system_view_stats.physical_disks",
            "type": "STAT"
        },
        {
            "display_name": "Collector Message Stats",
            "href": "http://localhost:8181/analytics/table/StatTable.SandeshMessageStat.msg_info",
            "name": "StatTable.SandeshMessageStat.msg_info",
            "type": "STAT"
        },
        {
            "display_name": "Sandesh Client Message Stats",
            "href": "http://localhost:8181/analytics/table/StatTable.ModuleClientState.tx_msg_diff",
            "name": "StatTable.ModuleClientState.tx_msg_diff",
            "type": "STAT"
        },
        {
            "display_name": "Sandesh Client Message-Type Stats",
            "href": "http://localhost:8181/analytics/table/StatTable.ModuleClientState.msg_type_diff",
            "name": "StatTable.ModuleClientState.msg_type_diff",
            "type": "STAT"
        },
        {
            "display_name": "Collector Database Table Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.CollectorDbStats.table_info",
            "name": "StatTable.CollectorDbStats.table_info",
            "type": "STAT"
        },
        {
            "display_name": "Collector Statistics Database Table",
            "href": "http://localhost:8181/analytics/table/StatTable.CollectorDbStats.stats_info",
            "name": "StatTable.CollectorDbStats.stats_info",
            "type": "STAT"
        },
        {
            "display_name": "Collector Database Errors",
            "href": "http://localhost:8181/analytics/table/StatTable.CollectorDbStats.errors",
            "name": "StatTable.CollectorDbStats.errors",
            "type": "STAT"
        },
        {
            "display_name": "Collector Database CQL Request Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.CollectorDbStats.cql_stats",
            "name": "StatTable.CollectorDbStats.cql_stats",
            "type": "STAT"
        },
        {
            "display_name": "Collector Database CQL Cluster Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.CollectorDbStats.cql_stats.stats",
            "name": "StatTable.CollectorDbStats.cql_stats.stats",
            "type": "STAT"
        },
        {
            "display_name": "Collector Database CQL Errors",
            "href": "http://localhost:8181/analytics/table/StatTable.CollectorDbStats.cql_stats.errors",
            "name": "StatTable.CollectorDbStats.cql_stats.errors",
            "type": "STAT"
        },
        {
            "display_name": "Values Table - string",
            "href": "http://localhost:8181/analytics/table/StatTable.FieldNames.fields",
            "name": "StatTable.FieldNames.fields",
            "type": "STAT"
        },
        {
            "display_name": "Values Table - integer",
            "href": "http://localhost:8181/analytics/table/StatTable.FieldNames.fieldi",
            "name": "StatTable.FieldNames.fieldi",
            "type": "STAT"
        },
        {
            "display_name": "QE Performance",
            "href": "http://localhost:8181/analytics/table/StatTable.QueryPerfInfo.query_stats",
            "name": "StatTable.QueryPerfInfo.query_stats",
            "type": "STAT"
        },
        {
            "display_name": "VN Agent",
            "href": "http://localhost:8181/analytics/table/StatTable.UveVirtualNetworkAgent.vn_stats",
            "name": "StatTable.UveVirtualNetworkAgent.vn_stats",
            "type": "STAT"
        },
        {
            "display_name": "VN ACL Rule Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.UveVirtualNetworkAgent.policy_rule_stats",
            "name": "StatTable.UveVirtualNetworkAgent.policy_rule_stats",
            "type": "STAT"
        },
        {
            "display_name": "Database Purge Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.DatabasePurgeInfo.stats",
            "name": "StatTable.DatabasePurgeInfo.stats",
            "type": "STAT"
        },
        {
            "display_name": "Database Usage Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.DatabaseUsageInfo.database_usage",
            "name": "StatTable.DatabaseUsageInfo.database_usage",
            "type": "STAT"
        },
        {
            "display_name": "Database Compaction Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.CassandraStatusData.cassandra_compaction_task",
            "name": "StatTable.CassandraStatusData.cassandra_compaction_task",
            "type": "STAT"
        },
        {
            "display_name": "Database Threadpool Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.CassandraStatusData.thread_pool_stats",
            "name": "StatTable.CassandraStatusData.thread_pool_stats",
            "type": "STAT"
        },
        {
            "display_name": "Analytics Protobuf Collector Transmit Socket Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.ProtobufCollectorStats.tx_socket_stats",
            "name": "StatTable.ProtobufCollectorStats.tx_socket_stats",
            "type": "STAT"
        },
        {
            "display_name": "Analytics Protobuf Collector Receive Socket Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.ProtobufCollectorStats.rx_socket_stats",
            "name": "StatTable.ProtobufCollectorStats.rx_socket_stats",
            "type": "STAT"
        },
        {
            "display_name": "Analytics Protobuf Collector Receive Message Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.ProtobufCollectorStats.rx_message_stats",
            "name": "StatTable.ProtobufCollectorStats.rx_message_stats",
            "type": "STAT"
        },
        {
            "display_name": "Analytics Protobuf Collector Database Table",
            "href": "http://localhost:8181/analytics/table/StatTable.ProtobufCollectorStats.db_table_info",
            "name": "StatTable.ProtobufCollectorStats.db_table_info",
            "type": "STAT"
        },
        {
            "display_name": "Analytics Protobuf Collector Statistics Database Table",
            "href": "http://localhost:8181/analytics/table/StatTable.ProtobufCollectorStats.db_statistics_table_info",
            "name": "StatTable.ProtobufCollectorStats.db_statistics_table_info",
            "type": "STAT"
        },
        {
            "display_name": "Analytics Protobuf Collector Database Errors",
            "href": "http://localhost:8181/analytics/table/StatTable.ProtobufCollectorStats.db_errors",
            "name": "StatTable.ProtobufCollectorStats.db_errors",
            "type": "STAT"
        },
        {
            "display_name": "Physical Router Fabric Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.TelemetryStream.enterprise.juniperNetworks.fabricMessageExt.edges.class_stats.transmit_counts",
            "name": "StatTable.TelemetryStream.enterprise.juniperNetworks.fabricMessageExt.edges.class_stats.transmit_counts",
            "type": "STAT"
        },
        {
            "display_name": "Underlay Flow Information",
            "href": "http://localhost:8181/analytics/table/StatTable.UFlowData.flow",
            "name": "StatTable.UFlowData.flow",
            "type": "STAT"
        },
        {
            "display_name": "Alarmgen UVE Key Stats",
            "href": "http://localhost:8181/analytics/table/StatTable.AlarmgenUpdate.o",
            "name": "StatTable.AlarmgenUpdate.o",
            "type": "STAT"
        },
        {
            "display_name": "Alarmgen Input Stats",
            "href": "http://localhost:8181/analytics/table/StatTable.AlarmgenUpdate.i",
            "name": "StatTable.AlarmgenUpdate.i",
            "type": "STAT"
        },
        {
            "display_name": "Alarmgen Counters",
            "href": "http://localhost:8181/analytics/table/StatTable.AlarmgenStatus.counters",
            "name": "StatTable.AlarmgenStatus.counters",
            "type": "STAT"
        },
        {
            "display_name": "Loadbalancer Listener Stats",
            "href": "http://localhost:8181/analytics/table/StatTable.LoadbalancerStats.listener",
            "name": "StatTable.LoadbalancerStats.listener",
            "type": "STAT"
        },
        {
            "display_name": "Loadbalancer Pool Stats",
            "href": "http://localhost:8181/analytics/table/StatTable.LoadbalancerStats.pool",
            "name": "StatTable.LoadbalancerStats.pool",
            "type": "STAT"
        },
        {
            "display_name": "Loadbalancer Member Stats",
            "href": "http://localhost:8181/analytics/table/StatTable.LoadbalancerStats.member",
            "name": "StatTable.LoadbalancerStats.member",
            "type": "STAT"
        },
        {
            "display_name": "Analytics Disk Usage Info",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.disk_usage_info",
            "name": "StatTable.NodeStatus.disk_usage_info",
            "type": "STAT"
        },
        {
            "display_name": "Database Disk Usage Info",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.disk_usage_info",
            "name": "StatTable.NodeStatus.disk_usage_info",
            "type": "STAT"
        },
        {
            "display_name": "Config Disk Usage Info",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.disk_usage_info",
            "name": "StatTable.NodeStatus.disk_usage_info",
            "type": "STAT"
        },
        {
            "display_name": "Control-node Disk Usage Info",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.disk_usage_info",
            "name": "StatTable.NodeStatus.disk_usage_info",
            "type": "STAT"
        },
        {
            "display_name": "Vrouter Disk Usage Info",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.disk_usage_info",
            "name": "StatTable.NodeStatus.disk_usage_info",
            "type": "STAT"
        },
        {
            "display_name": "Analytics Process Memory CPU Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.process_mem_cpu_usage",
            "name": "StatTable.NodeStatus.process_mem_cpu_usage",
            "type": "STAT"
        },
        {
            "display_name": "Database Process Memory CPU Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.process_mem_cpu_usage",
            "name": "StatTable.NodeStatus.process_mem_cpu_usage",
            "type": "STAT"
        },
        {
            "display_name": "Config Process Memory CPU Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.process_mem_cpu_usage",
            "name": "StatTable.NodeStatus.process_mem_cpu_usage",
            "type": "STAT"
        },
        {
            "display_name": "Control-node Process Memory CPU Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.process_mem_cpu_usage",
            "name": "StatTable.NodeStatus.process_mem_cpu_usage",
            "type": "STAT"
        },
        {
            "display_name": "Vrouter Process Memory CPU Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.process_mem_cpu_usage",
            "name": "StatTable.NodeStatus.process_mem_cpu_usage",
            "type": "STAT"
        },
        {
            "display_name": "Analytics System Memory Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.system_mem_usage",
            "name": "StatTable.NodeStatus.system_mem_usage",
            "type": "STAT"
        },
        {
            "display_name": "Database System Memory Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.system_mem_usage",
            "name": "StatTable.NodeStatus.system_mem_usage",
            "type": "STAT"
        },
        {
            "display_name": "Config System Memory Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.system_mem_usage",
            "name": "StatTable.NodeStatus.system_mem_usage",
            "type": "STAT"
        },
        {
            "display_name": "Control-node System Memory Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.system_mem_usage",
            "name": "StatTable.NodeStatus.system_mem_usage",
            "type": "STAT"
        },
        {
            "display_name": "Vrouter System Memory Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.system_mem_usage",
            "name": "StatTable.NodeStatus.system_mem_usage",
            "type": "STAT"
        },
        {
            "display_name": "Analytics System CPU Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.system_cpu_usage",
            "name": "StatTable.NodeStatus.system_cpu_usage",
            "type": "STAT"
        },
        {
            "display_name": "Database System CPU Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.system_cpu_usage",
            "name": "StatTable.NodeStatus.system_cpu_usage",
            "type": "STAT"
        },
        {
            "display_name": "Config System CPU Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.system_cpu_usage",
            "name": "StatTable.NodeStatus.system_cpu_usage",
            "type": "STAT"
        },
        {
            "display_name": "Control-node System CPU Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.system_cpu_usage",
            "name": "StatTable.NodeStatus.system_cpu_usage",
            "type": "STAT"
        },
        {
            "display_name": "Vrouter System CPU Usage",
            "href": "http://localhost:8181/analytics/table/StatTable.NodeStatus.system_cpu_usage",
            "name": "StatTable.NodeStatus.system_cpu_usage",
            "type": "STAT"
        },
        {
            "display_name": "Virtual Machine Floating IP Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.UveVMInterfaceAgent.fip_diff_stats",
            "name": "StatTable.UveVMInterfaceAgent.fip_diff_stats",
            "type": "STAT"
        },
        {
            "display_name": "VMI ACL Rule Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.UveVMInterfaceAgent.sg_rule_stats",
            "name": "StatTable.UveVMInterfaceAgent.sg_rule_stats",
            "type": "STAT"
        },
        {
            "display_name": "Virtual Machine Interface Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.UveVMInterfaceAgent.if_stats",
            "name": "StatTable.UveVMInterfaceAgent.if_stats",
            "type": "STAT"
        },
        {
            "display_name": "Vrouter Flow Setup Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.VrouterStatsAgent.flow_rate",
            "name": "StatTable.VrouterStatsAgent.flow_rate",
            "type": "STAT"
        },
        {
            "display_name": "Vrouter IFMAP Parse Error Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.VrouterStatsAgent.ifmap_stats",
            "name": "StatTable.VrouterStatsAgent.ifmap_stats",
            "type": "STAT"
        },
        {
            "display_name": "Analytics API Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.AnalyticsApiStats.api_stats",
            "name": "StatTable.AnalyticsApiStats.api_stats",
            "type": "STAT"
        },
        {
            "display_name": "Api Server Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.VncApiStatsLog.api_stats",
            "name": "StatTable.VncApiStatsLog.api_stats",
            "type": "STAT"
        },
        {
            "display_name": "Vrouter Physical Interface Input bandwidth Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.VrouterStatsAgent.phy_band_in_bps",
            "name": "StatTable.VrouterStatsAgent.phy_band_in_bps",
            "type": "STAT"
        },
        {
            "display_name": "Vrouter Physical Interface Output bandwidth Statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.VrouterStatsAgent.phy_band_out_bps",
            "name": "StatTable.VrouterStatsAgent.phy_band_out_bps",
            "type": "STAT"
        },
        {
            "display_name": "Routing Instance Information",
            "href": "http://localhost:8181/analytics/table/StatTable.RoutingInstanceStatsData.table_stats",
            "name": "StatTable.RoutingInstanceStatsData.table_stats",
            "type": "STAT"
        },
        {
            "display_name": "User defined log statistics",
            "href": "http://localhost:8181/analytics/table/StatTable.UserDefinedLogStat.count",
            "name": "StatTable.UserDefinedLogStat.count",
            "type": "STAT"
        },
    ]
    root@a1s33:~# 
    
Example of a query into message table::

    [root@a3s14 ~]# cat filename 
    {"sort": 1, "start_time": 1366841256508546, "sort_fields": ["MessageTS"], "filter": [{"name": "Type", "value": "1", "op": 1}], "end_time": 1366841856508560, "select_fields": ["MessageTS", "Source", "ModuleId", "Category", "Messagetype", "SequenceNum", "Xmlmessage", "Type"], "table": "MessageTable", "where": [[{"name": "ModuleId", "value": "ControlNode", "op": 1}, {"name": "Messagetype", "value": "BgpPeerMessageLog", "op": 1}]]}
    [root@a3s14 ~]# 
    [root@a3s14 ~]# 
    [root@a3s14 ~]# curl -X POST --data @filename 127.0.0.1:8081/analytics/query --header "Content-Type:application/json" | python -mjson.tool
      % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                     Dload  Upload   Total   Spent    Left  Speed
    100  183k  100  183k  100   431  1282k   3021 --:--:-- --:--:-- --:--:-- 1288k
    {
        "value": [
            {
                "Category": "BGP", 
                "MessageTS": 1366841263859397, 
                "Messagetype": "BgpPeerMessageLog", 
                "ModuleId": "ControlNode", 
                "SequenceNum": 130991, 
                "Source": "a3s16", 
                "Type": 1, 
                "Xmlmessage": "<BgpPeerMessageLog type=\"sandesh\"><PeerType type=\"string\" identifier=\"1\">Bgp</PeerType><str2 type=\"string\" identifier=\"2\">Peer</str2><Peer type=\"string\" identifier=\"3\">10.84.11.252</Peer><Direction type=\"string\" identifier=\"4\">&gt;</Direction><Message type=\"string\" identifier=\"5\">Send Keepalive 19 bytes</Message><file type=\"string\" identifier=\"-32768\">src/bgp/bgp_peer.cc</file><line type=\"i32\" identifier=\"-32767\">494</line></BgpPeerMessageLog>"
            }, 
            {
                "Category": "BGP", 
                "MessageTS": 1366841263859499, 
                "Messagetype": "BgpPeerMessageLog", 
                "ModuleId": "ControlNode", 
                "SequenceNum": 130992, 
                "Source": "a3s16", 
                "Type": 1, 
                "Xmlmessage": "<BgpPeerMessageLog type=\"sandesh\"><PeerType type=\"string\" identifier=\"1\">Bgp</PeerType><str2 type=\"string\" identifier=\"2\">Peer</str2><Peer type=\"string\" identifier=\"3\">10.84.11.252</Peer><Direction type=\"string\" identifier=\"4\">:</Direction><Message type=\"string\" identifier=\"5\">Initiating close process</Message><file type=\"string\" identifier=\"-32768\">src/bgp/bgp_peer_close.cc</file><line type=\"i32\" identifier=\"-32767\">260</line></BgpPeerMessageLog>"
            }, 
            {
                "Category": "BGP", 
                "MessageTS": 1366841263859742, 
                "Messagetype": "BgpPeerMessageLog", 
                "ModuleId": "ControlNode", 
                "SequenceNum": 130993, 
                "Source": "a3s16", 
                "Type": 1, 
                "Xmlmessage": "<BgpPeerMessageLog type=\"sandesh\"><PeerType type=\"string\" identifier=\"1\">Bgp</PeerType><str2 type=\"string\" identifier=\"2\">Peer</str2><Peer type=\"string\" identifier=\"3\">10.84.11.252</Peer><Direction type=\"string\" identifier=\"4\">:</Direction><Message type=\"string\" identifier=\"5\">Unregister peer from all tables</Message><file type=\"string\" identifier=\"-32768\">src/bgp/bgp_peer_membership.cc</file><line type=\"i32\" identifier=\"-32767\">647</line></BgpPeerMessageLog>"
            }, 
            {
                "Category": "BGP", 
                "MessageTS": 1366841263859821, 
                "Messagetype": "BgpPeerMessageLog", 
                "ModuleId": "ControlNode", 
                "SequenceNum": 130994, 
                "Source": "a3s16", 
                "Type": 1, 
                "Xmlmessage": "<BgpPeerMessageLog type=\"sandesh\"><PeerType type=\"string\" identifier=\"1\">Bgp</PeerType><str2 type=\"string\" identifier=\"2\">Peer</str2><Peer type=\"string\" identifier=\"3\">10.84.11.252</Peer><Direction type=\"string\" identifier=\"4\">:</Direction><Message type=\"string\" identifier=\"5\">Close process is complete</Message><file type=\"string\" identifier=\"-32768\">src/bgp/bgp_peer_close.cc</file><line type=\"i32\" identifier=\"-32767\">162</line></BgpPeerMessageLog>"
            }, 
            ...
    
