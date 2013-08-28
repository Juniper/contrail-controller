opserver Package
================

:mod:`opserver` Module
----------------------
Opserver provides ReST API interface to extract the operational state of
Juniper's Contrail Virtual Network System.

User Visible Entities
^^^^^^^^^^^^^^^^^^^^^
Opserver provides API to get the
current state of the User Visible Entities in the Contrail VNS. ``User Visible
Entity(UVE)`` is defined as an Object that may span multiple components and may
require aggregation before the UVE information is presented. The UVEs supported
in Contrail VNS include the following:

    * ``Virtual Network``: This UVE provides information associated with a virtual network such as:
        - list of networks connected to this network
        - list of virtual machines spawned in this VN
        - list of ACLs associated with this VN
        - global input/output statistics
        - per VN pair input/output statistics

        The ReST api to get this UVE is through **HTTP GET** using the URL ``/analytics/virtual-network/<name>``
    * ``Virtual Machine``: This UVE provides information associated with a virtual machine such as:
        - list of interfaces in this virtual machine
        - list of floating IPs associated with each of it's interfaces
        - input/output statistics

        The ReST api to get this UVE is through **HTTP GET** using the URL ``/analytics/virtual-machine/<name>``
    * ``VRouter``: This UVE provides information associated with a VRouter such as:
        - list of virtual networks present on this VRouter
        - list of virtual machines spawned on the server of this VRouter
        - statistics of the traffic flowing through this VRouter

        The ReST api to get this UVE is through **HTTP GET** using the URL ``/analytics/vrouter/<name>``

Log and Flow Information
^^^^^^^^^^^^^^^^^^^^^^^^
In Contrail VNS, the Log and Flow information is collected and stored centrally
using horizontally scalable Contrail VNS Collector and horizontally scalable
NoSQL database. Opserver provides ReST API to extract this information via
queries. The queries provide well known SQL syntax and hide the underlying
complexity of NoSQL tables.

The following are the **HTTP GET** APIs related to supported queries:
    * ``/analytics/tables``
        this API gives the list of SQL-type tables available for querying and the hrefs to get information for each of these tables
    * ``/analytics/table/<table>``
        this API gives for a given table, list of APIs available to get information for this table
    * ``/analytics/table/<table>/schema``
        this API gives schema for a given table
    * ``/analytics/table/<table>/column-values``
        this API gives hrefs for the column-names that support list of valid values
    * ``/analytics/table/<table>/column-values/<column>``
        this API gives valid values for a given column-name

The following is the **HTTP POST** API related to these queries:
    * ``/analytics/query``
        * This API is to extract information from one of the tables. The format of the query follows the SQL syntax of the following form

            | SELECT field1, field2...
            | FROM table1
            | WHERE field1 = value1 AND field3 = value3...
            | FILTER BY...
            | SORT BY...
            | LIMIT 

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

Example Outputs
^^^^^^^^^^^^^^^

Example output for a virtual network UVE::

    [root@a3s14 ~]# curl 127.0.0.1:8081/analytics/virtual-network/default-domain:demo:front-end | python -mjson.tool
      % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                     Dload  Upload   Total   Spent    Left  Speed
    100  2576  100  2576    0     0   152k      0 --:--:-- --:--:-- --:--:--  157k
    {
        "UveVirtualNetworkAgent": {
            "acl": [
                [
                    {
                        "@type": "string"
                    }, 
                    "a3s18:VRouterAgent"
                ]
            ], 
            "in_bytes": {
                "#text": "2232972057", 
                "@aggtype": "counter", 
                "@type": "i64"
            }, 
            "in_stats": {
                "@aggtype": "append", 
                "@type": "list", 
                "list": {
                    "@size": "3", 
                    "@type": "struct", 
                    "UveInterVnStats": [
                        {
                            "bytes": {
                                "#text": "2114516371", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "default-domain:demo:back-end", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "5122001", 
                                "@type": "i64"
                            }
                        }, 
                        {
                            "bytes": {
                                "#text": "1152123", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "__FABRIC__", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "11323", 
                                "@type": "i64"
                            }
                        }, 
                        {
                            "bytes": {
                                "#text": "8192", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "default-domain:demo:front-end", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "50", 
                                "@type": "i64"
                            }
                        }
                    ]
                }
            }, 
            "in_tpkts": {
                "#text": "5156342", 
                "@aggtype": "counter", 
                "@type": "i64"
            }, 
            "interface_list": {
                "@aggtype": "union", 
                "@type": "list", 
                "list": {
                    "@size": "1", 
                    "@type": "string", 
                    "element": [
                        "tap2158f77c-ec"
                    ]
                }
            }, 
            "out_bytes": {
                "#text": "2187615961", 
                "@aggtype": "counter", 
                "@type": "i64"
            }, 
            "out_stats": {
                "@aggtype": "append", 
                "@type": "list", 
                "list": {
                    "@size": "4", 
                    "@type": "struct", 
                    "UveInterVnStats": [
                        {
                            "bytes": {
                                "#text": "2159083215", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "default-domain:demo:back-end", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "5143693", 
                                "@type": "i64"
                            }
                        }, 
                        {
                            "bytes": {
                                "#text": "1603041", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "__FABRIC__", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "9595", 
                                "@type": "i64"
                            }
                        }, 
                        {
                            "bytes": {
                                "#text": "24608", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "__UNKNOWN__", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "408", 
                                "@type": "i64"
                            }
                        }, 
                        {
                            "bytes": {
                                "#text": "8192", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "default-domain:demo:front-end", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "50", 
                                "@type": "i64"
                            }
                        }
                    ]
                }
            }, 
            "out_tpkts": {
                "#text": "5134830", 
                "@aggtype": "counter", 
                "@type": "i64"
            }, 
            "virtualmachine_list": {
                "@aggtype": "union", 
                "@type": "list", 
                "list": {
                    "@size": "1", 
                    "@type": "string", 
                    "element": [
                        "dd09f8c3-32a8-456f-b8cc-fab15189f50f"
                    ]
                }
            }
        }, 
        "UveVirtualNetworkConfig": {
            "connected_networks": {
                "@aggtype": "union", 
                "@type": "list", 
                "list": {
                    "@size": "1", 
                    "@type": "string", 
                    "element": [
                        "default-domain:demo:back-end"
                    ]
                }
            }, 
            "routing_instance_list": {
                "@aggtype": "union", 
                "@type": "list", 
                "list": {
                    "@size": "1", 
                    "@type": "string", 
                    "element": [
                        "front-end"
                    ]
                }
            }, 
            "total_acl_rules": [
                [
                    {
                        "#text": "3", 
                        "@type": "i32"
                    }, 
                    ":", 
                    "a3s14:Schema"
                ]
            ]
        }
    }
    
Example output for a virtual machine UVE::
    
    [root@a3s14 ~]# curl 127.0.0.1:8081/analytics/virtual-machine/f38eb47e-63d2-4b39-80de-8fe68e6af1e4 | python -mjson.tool
      % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                     Dload  Upload   Total   Spent    Left  Speed
    100   736  100   736    0     0   160k      0 --:--:-- --:--:-- --:--:--  179k
    {
        "UveVirtualMachineAgent": {
            "interface_list": [
                [
                    {
                        "@type": "list", 
                        "list": {
                            "@size": "1", 
                            "@type": "struct", 
                            "VmInterfaceAgent": [
                                {
                                    "in_bytes": {
                                        "#text": "2188895907", 
                                        "@aggtype": "counter", 
                                        "@type": "i64"
                                    }, 
                                    "in_pkts": {
                                        "#text": "5130901", 
                                        "@aggtype": "counter", 
                                        "@type": "i64"
                                    }, 
                                    "ip_address": {
                                        "#text": "192.168.2.253", 
                                        "@type": "string"
                                    }, 
                                    "name": {
                                        "#text": "f38eb47e-63d2-4b39-80de-8fe68e6af1e4:ccb085a0-c994-4034-be0f-6fd5ad08ce83", 
                                        "@type": "string"
                                    }, 
                                    "out_bytes": {
                                        "#text": "2201821626", 
                                        "@aggtype": "counter", 
                                        "@type": "i64"
                                    }, 
                                    "out_pkts": {
                                        "#text": "5153526", 
                                        "@aggtype": "counter", 
                                        "@type": "i64"
                                    }, 
                                    "virtual_network": {
                                        "#text": "default-domain:demo:back-end", 
                                        "@aggtype": "listkey", 
                                        "@type": "string"
                                    }
                                }
                            ]
                        }
                    }, 
                    "a3s19:VRouterAgent"
                ]
            ]
        }
    }
    
Example output for a vrouter UVE::
    
    [root@a3s14 ~]# curl 127.0.0.1:8081/analytics/vrouter/a3s18 | python -mjson.tool
      % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                     Dload  Upload   Total   Spent    Left  Speed
    100   706  100   706    0     0   142k      0 --:--:-- --:--:-- --:--:--  172k
    {
        "VrouterAgent": {
            "collector": [
                [
                    {
                        "#text": "10.84.17.1", 
                        "@type": "string"
                    }, 
                    "a3s18:VRouterAgent"
                ]
            ], 
            "connected_networks": [
                [
                    {
                        "@type": "list", 
                        "list": {
                            "@size": "1", 
                            "@type": "string", 
                            "element": [
                                "default-domain:demo:front-end"
                            ]
                        }
                    }, 
                    "a3s18:VRouterAgent"
                ]
            ], 
            "interface_list": [
                [
                    {
                        "@type": "list", 
                        "list": {
                            "@size": "1", 
                            "@type": "string", 
                            "element": [
                                "tap2158f77c-ec"
                            ]
                        }
                    }, 
                    "a3s18:VRouterAgent"
                ]
            ], 
            "virtual_machine_list": [
                [
                    {
                        "@type": "list", 
                        "list": {
                            "@size": "1", 
                            "@type": "string", 
                            "element": [
                                "dd09f8c3-32a8-456f-b8cc-fab15189f50f"
                            ]
                        }
                    }, 
                    "a3s18:VRouterAgent"
                ]
            ], 
            "xmpp_peer_list": [
                [
                    {
                        "@type": "list", 
                        "list": {
                            "@size": "2", 
                            "@type": "string", 
                            "element": [
                                "10.84.17.2", 
                                "10.84.17.3"
                            ]
                        }
                    }, 
                    "a3s18:VRouterAgent"
                ]
            ]
        }
    }

Examples of queries related to the VNS log and flow data::

    [root@a3s14 ~]# curl 127.0.0.1:8081/analytics/tables | python -mjson.tool
      % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                     Dload  Upload   Total   Spent    Left  Speed
    100   846  100   846    0     0   509k      0 --:--:-- --:--:-- --:--:--  826k
    [
        {
            "href": "http://127.0.0.1:8081/analytics/table/MessageTable", 
            "name": "MessageTable"
        }, 
        {
            "href": "http://127.0.0.1:8081/analytics/table/ObjectVNTable", 
            "name": "ObjectVNTable"
        }, 
        {
            "href": "http://127.0.0.1:8081/analytics/table/ObjectVMTable", 
            "name": "ObjectVMTable"
        }, 
        {
            "href": "http://127.0.0.1:8081/analytics/table/ObjectVRouter", 
            "name": "ObjectVRouter"
        }, 
        {
            "href": "http://127.0.0.1:8081/analytics/table/ObjectBgpPeer", 
            "name": "ObjectBgpPeer"
        }, 
        {
            "href": "http://127.0.0.1:8081/analytics/table/ObjectRoutingInstance", 
            "name": "ObjectRoutingInstance"
        }, 
        {
            "href": "http://127.0.0.1:8081/analytics/table/ObjectXmppConnection", 
            "name": "ObjectXmppConnection"
        }, 
        {
            "href": "http://127.0.0.1:8081/analytics/table/FlowRecordTable", 
            "name": "FlowRecordTable"
        }, 
        {
            "href": "http://127.0.0.1:8081/analytics/table/FlowSeriesTable", 
            "name": "FlowSeriesTable"
        }
    ]
    [root@a3s14 ~]# curl 127.0.0.1:8081/analytics/table/MessageTable | python -mjson.tool
      % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                     Dload  Upload   Total   Spent    Left  Speed
    100   192  100   192    0     0   102k      0 --:--:-- --:--:-- --:--:--  187k
    [
        {
            "href": "http://127.0.0.1:8081/analytics/table/MessageTable/schema", 
            "name": "schema"
        }, 
        {
            "href": "http://127.0.0.1:8081/analytics/table/MessageTable/column-values", 
            "name": "column-values"
        }
    ]
    [root@a3s14 ~]# curl 127.0.0.1:8081/analytics/table/MessageTable/schema | python -mjson.tool
      % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                     Dload  Upload   Total   Spent    Left  Speed
    100   630  100   630    0     0   275k      0 --:--:-- --:--:-- --:--:--  307k
    {
        "columns": [
            {
                "datatype": "int", 
                "index": "False", 
                "name": "MessageTS"
            }, 
            {
                "datatype": "string", 
                "index": "True", 
                "name": "Source"
            }, 
            {
                "datatype": "string", 
                "index": "True", 
                "name": "ModuleId"
            }, 
            {
                "datatype": "string", 
                "index": "True", 
                "name": "Category"
            }, 
            {
                "datatype": "int", 
                "index": "True", 
                "name": "Level"
            }, 
            {
                "datatype": "int", 
                "index": "False", 
                "name": "Type"
            }, 
            {
                "datatype": "string", 
                "index": "True", 
                "name": "Messagetype"
            }, 
            {
                "datatype": "int", 
                "index": "False", 
                "name": "SequenceNum"
            }, 
            {
                "datatype": "string", 
                "index": "False", 
                "name": "Context"
            }, 
            {
                "datatype": "string", 
                "index": "False", 
                "name": "Xmlmessage"
            }
        ], 
        "type": "LOG"
    }

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
    

:mod:`opserver_client` Module
-----------------------------

This module provides functionality to exercise ReST API interface to Contrail
VNS Opserver.

:mod:`opserver` Classes and Functions Documentation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. automodule:: opserver.opserver_client
    :members:
    :show-inheritance:

Example Outputs
^^^^^^^^^^^^^^^

Example output for a virtual network UVE::
    
    >>> client.get_uve_state('default-domain:demo:front-end', 'virtual-network')
    http://127.0.0.1:8081/analytics/virtual-network/default-domain:demo:front-end
    {
        "UveVirtualNetworkAgent": {
            "acl": [
                [
                    {
                        "@type": "string"
                    }, 
                    "a3s18:VRouterAgent"
                ]
            ], 
            "in_bytes": {
                "#text": "2274362383", 
                "@aggtype": "counter", 
                "@type": "i64"
            }, 
            "in_stats": {
                "@aggtype": "append", 
                "@type": "list", 
                "list": {
                    "@size": "3", 
                    "@type": "struct", 
                    "UveInterVnStats": [
                        {
                            "bytes": {
                                "#text": "2154615631", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "default-domain:demo:back-end", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "5194580", 
                                "@type": "i64"
                            }
                        }, 
                        {
                            "bytes": {
                                "#text": "1152123", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "__FABRIC__", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "11323", 
                                "@type": "i64"
                            }
                        }, 
                        {
                            "bytes": {
                                "#text": "8192", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "default-domain:demo:front-end", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "50", 
                                "@type": "i64"
                            }
                        }
                    ]
                }
            }, 
            "in_tpkts": {
                "#text": "5230101", 
                "@aggtype": "counter", 
                "@type": "i64"
            }, 
            "interface_list": {
                "@aggtype": "union", 
                "@type": "list", 
                "list": {
                    "@size": "1", 
                    "@type": "string", 
                    "element": [
                        "tap2158f77c-ec"
                    ]
                }
            }, 
            "out_bytes": {
                "#text": "2228756965", 
                "@aggtype": "counter", 
                "@type": "i64"
            }, 
            "out_stats": {
                "@aggtype": "append", 
                "@type": "list", 
                "list": {
                    "@size": "4", 
                    "@type": "struct", 
                    "UveInterVnStats": [
                        {
                            "bytes": {
                                "#text": "2199424147", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "default-domain:demo:back-end", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "5216946", 
                                "@type": "i64"
                            }
                        }, 
                        {
                            "bytes": {
                                "#text": "1603041", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "__FABRIC__", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "9595", 
                                "@type": "i64"
                            }
                        }, 
                        {
                            "bytes": {
                                "#text": "24608", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "__UNKNOWN__", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "408", 
                                "@type": "i64"
                            }
                        }, 
                        {
                            "bytes": {
                                "#text": "8192", 
                                "@type": "i64"
                            }, 
                            "other_vn": {
                                "#text": "default-domain:demo:front-end", 
                                "@aggtype": "listkey", 
                                "@type": "string"
                            }, 
                            "tpkts": {
                                "#text": "50", 
                                "@type": "i64"
                            }
                        }
                    ]
                }
            }, 
            "out_tpkts": {
                "#text": "5207916", 
                "@aggtype": "counter", 
                "@type": "i64"
            }, 
            "virtualmachine_list": {
                "@aggtype": "union", 
                "@type": "list", 
                "list": {
                    "@size": "1", 
                    "@type": "string", 
                    "element": [
                        "dd09f8c3-32a8-456f-b8cc-fab15189f50f"
                    ]
                }
            }
        }, 
        "UveVirtualNetworkConfig": {
            "connected_networks": {
                "@aggtype": "union", 
                "@type": "list", 
                "list": {
                    "@size": "1", 
                    "@type": "string", 
                    "element": [
                        "default-domain:demo:back-end"
                    ]
                }
            }, 
            "routing_instance_list": {
                "@aggtype": "union", 
                "@type": "list", 
                "list": {
                    "@size": "1", 
                    "@type": "string", 
                    "element": [
                        "front-end"
                    ]
                }
            }, 
            "total_acl_rules": [
                [
                    {
                        "#text": "3", 
                        "@type": "i32"
                    }, 
                    ":", 
                    "a3s14:Schema"
                ]
            ]
        }
    }
    
Example output for a table query::
    
    >>> 
    >>> client.query(table='MessageTable', ...
