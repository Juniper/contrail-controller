#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

alarm_list = [
    {
        "alarm_rules": {
            "or_list" : [
                {
                    "and_list": [
                        {
                            "operand1": "ContrailConfig.elements.virtual_router_ip_address",
                            "operation": "!=",
                            "operand2": {
                                "uve_attribute": "VrouterAgent.control_ip"
                            }
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-address-mismatch-compute"
        ],
        "id_perms": {
            "description": "Compute Node IP Address mismatch."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "ContrailConfig.elements.bgp_router_parameters.address",
                            "operation": "not in",
                            "operand2": {
                                "uve_attribute":
                                    "BgpRouterState.bgp_router_ip_list"
                            }
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-address-mismatch-control"
        ],
        "id_perms": {
            "description": "Control Node IP Address mismatch."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "control-node"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "BgpRouterState.num_up_bgp_peer",
                            "operation": "==",
                            "operand2": {
                                "json_value": 'null'
                            }
                        }
                    ]
                },
                {
                    "and_list": [
                        {
                            "operand1": "BgpRouterState.num_up_bgp_peer",
                            "operation": "!=",
                            "operand2": {
                                "uve_attribute": "BgpRouterState.num_bgp_peer"
                            }
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-bgp-connectivity"
        ],
        "id_perms": {
            "description": "BGP peer mismatch. Not enough BGP peers are up."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "control-node"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "ContrailConfig",
                            "operation": "==",
                            "operand2": {
                                "json_value": "null"
                            }
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-conf-incorrect"
        ],
        "id_perms": {
            "description": "ContrailConfig missing or incorrect. Configuration pushed to Ifmap as ContrailConfig is missing/incorrect."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "analytics-node",
                "analytics-alarm-node",
                "analytics-snmp-node",
                "config-node",
                "control-node",
                "database-node",
                "config-database-node",
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "NodeStatus.disk_usage_info.*.percentage_partition_space_used",
                            "operation": "range",
                            "operand2": {
                                "json_value": "[70, 90]"
                            },
                            "variables":
                                ["NodeStatus.disk_usage_info.__key"]
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-disk-usage-high"
        ],
        "id_perms": {
            "description": "Disk usage crosses high threshold limit."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "analytics-node",
                "analytics-alarm-node",
                "analytics-snmp-node",
                "config-node",
                "control-node",
                "database-node",
                "config-database-node",
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "NodeStatus.disk_usage_info.*.percentage_partition_space_used",
                            "operation": ">",
                            "operand2": {
                                "json_value": "90"
                            },
                            "variables":
                                ["NodeStatus.disk_usage_info.__key"]
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 0,
        "fq_name": [
            "default-global-system-config",
            "system-defined-disk-usage-critical"
        ],
        "id_perms": {
            "description": "Disk usage crosses critical threshold limit."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "analytics-node",
                "analytics-alarm-node",
                "analytics-snmp-node",
                "config-node",
                "control-node",
                "database-node",
                "config-database-node",
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "NodeStatus",
                            "operation": "==",
                            "operand2": {
                                "json_value": "null"
                            }
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 0,
        "fq_name": [
            "default-global-system-config",
            "system-defined-node-status"
        ],
        "id_perms": {
            "description": "Node Failure. NodeStatus UVE not present."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "analytics-node",
                "analytics-alarm-node",
                "analytics-snmp-node",
                "config-node",
                "control-node",
                "database-node",
                "config-database-node",
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "NodeStatus.build_info",
                            "operation": "==",
                            "operand2": {
                                "json_value": "null"
                            }
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-partial-sysinfo"
        ],
        "id_perms": {
            "description": "System Info Incomplete."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "analytics-node",
                "analytics-alarm-node",
                "analytics-snmp-node",
                "config-node",
                "control-node",
                "database-node",
                "config-database-node",
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "NodeStatus.process_status",
                            "operation": "==",
                            "operand2": {
                                "json_value": "null"
                            }
                        }
                    ]
                },
                {
                    "and_list": [
                        {
                            "operand1": "NodeStatus.process_status.state",
                            "operation": "!=",
                            "operand2": {
                                "json_value": "\"Functional\""
                            },
                            "variables": ["NodeStatus.process_status.module_id",
                                "NodeStatus.process_status.instance_id"]
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 0,
        "fq_name": [
            "default-global-system-config",
            "system-defined-process-connectivity"
        ],
        "id_perms": {
            "description": "Process(es) reporting as non-functional."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "analytics-node",
                "analytics-alarm-node",
                "analytics-snmp-node",
                "config-node",
                "control-node",
                "database-node",
                "config-database-node",
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "NodeStatus.process_info",
                            "operation": "==",
                            "operand2": {
                                "json_value": "null"
                            }
                        }
                    ]
                },
                {
                    "and_list": [
                        {
                            "operand1": "NodeStatus.process_info.process_state",
                            "operation": "!=",
                            "operand2": {
                                "json_value": "\"PROCESS_STATE_RUNNING\""
                            },
                            "variables": ["NodeStatus.process_info.process_name"]
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 0,
        "fq_name": [
            "default-global-system-config",
            "system-defined-process-status"
        ],
        "id_perms": {
            "description": "Process Failure."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "analytics-node",
                "analytics-alarm-node",
                "analytics-snmp-node",
                "config-node",
                "control-node",
                "database-node",
                "config-database-node",
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "ContrailConfig.elements.virtual_router_refs",
                            "operation": "!=",
                            "operand2": {
                                "json_value": "null"
                            }
                        },
                        {
                            "operand1": "ProuterData.connected_agent_list",
                            "operation": "size!=",
                            "operand2": {
                                "json_value": "1"
                            }
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-prouter-connectivity"
        ],
        "id_perms": {
            "description": "Prouter connectivity to controlling tor agent does not exist we look for non-empty value for connected_agent_list"
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "prouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "ContrailConfig.elements.virtual_router_refs",
                            "operation": "!=",
                            "operand2": {
                                "json_value": "null"
                            }
                        },
                        {
                            "operand1": "ProuterData.tsn_agent_list",
                            "operation": "size!=",
                            "operand2": {
                                "json_value": "1"
                            }
                       },
                       {
                           "operand1": "ProuterData.gateway_mode",
                           "operation": "!=",
                           "operand2": {
                               "json_value": "\"SERVER\""
                           }
                       }
                    ]
                },
                {
                    "and_list": [
                        {
                            "operand1": "ContrailConfig.elements.virtual_router_refs",
                            "operation": "!=",
                            "operand2": {
                                "json_value": "null"
                            }
                        },
                        {
                            "operand1": "ProuterData.tsn_agent_list",
                            "operation": "size!=",
                            "operand2": {
                                "json_value": "0"
                            }
                       },
                       {
                           "operand1": "ProuterData.gateway_mode",
                           "operation": "==",
                           "operand2": {
                               "json_value": "\"SERVER\""
                           }
                       }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-prouter-tsn-connectivity"
        ],
        "id_perms": {
            "description": "Prouter connectivity to controlling tsn agent does not exist we look for non-empty value for tsn_agent_list"
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "prouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "StorageCluster.info_stats.status",
                            "operation": "!=",
                            "operand2": {
                                "json_value": "0"
                            },
                            "variables":
                                ["StorageCluster.info_stats.health_summary"]
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-storage-cluster-state"
        ],
        "id_perms": {
            "description": "Storage Cluster warning/errors."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "storage-cluster"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "VrouterAgent.down_interface_count",
                            "operation": ">=",
                            "operand2": {
                                "json_value": "1"
                            },
                            "variables": ["VrouterAgent.error_intf_list",
                                          "VrouterAgent.no_config_intf_list"]
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-vrouter-interface"
        ],
        "id_perms": {
            "description": "Vrouter interface(s) down."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "BgpRouterState.num_up_xmpp_peer",
                            "operation": "==",
                            "operand2": {
                                "json_value": "null"
                            }
                        }
                    ]
                },
                {
                    "and_list": [
                        {
                            "operand1": "BgpRouterState.num_up_xmpp_peer",
                            "operation": "!=",
                            "operand2": {
                                "uve_attribute": "BgpRouterState.num_xmpp_peer"
                            }
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-xmpp-connectivity"
        ],
        "id_perms": {
            "description": "XMPP peer mismatch."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "control-node"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "XmppPeerInfoData.close_reason",
                            "operation": "!=",
                            "operand2": {
                                "json_value": "null"
                            }
                        },
                        {
                            "operand1": "XmppPeerInfoData.state_info.state",
                            "operation": "!=",
                            "operand2": {
                                "json_value": "\"Established\""
                            }
                        },
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-xmpp-close-reason"
        ],
        "id_perms": {
            "description": "XMPP connection closed towards peer, \
                            alarm has close reason"
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "xmpp-peer"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "NodeStatus.all_core_file_list",
                            "operand2": {
                                "json_value": "null"
                            },
                            "operation": "!="
                        },
                        {
                            "operand1": "NodeStatus.all_core_file_list",
                            "operand2": {
                                "json_value": "0"
                            },
                            "operation": "size!="
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 0,
        "fq_name": [
            "default-global-system-config",
            "system-defined-core-files"
        ],
        "id_perms": {
            "description": "A core file has been generated on the node."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "analytics-node",
                "analytics-alarm-node",
                "analytics-snmp-node",
                "config-node",
                "control-node",
                "database-node",
                "config-database-node",
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "CassandraStatusData.cassandra_compaction_task.pending_compaction_tasks",
                            "operand2": {
                                "json_value": "300"
                            },
                            "operation": ">="
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-pending-cassandra-compaction-tasks"
        ],
        "parent_type": "global-system-config",
        "id_perms": {
            "description": "Pending compaction tasks in cassandra crossed the configured threshold."
        },
        "uve_keys": {
            "uve_key": [
                "database-node"
                "config-database-node",
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "NodeStatus.running_package_version",
                            "operation": "!=",
                            "operand2": {
                                "uve_attribute": "NodeStatus.installed_package_version"
                            }

                        }
                    ]
                }
            ]
        },
        "alarm_severity": 0,
        "fq_name": [
            "default-global-system-config",
            "system-defined-package-version-mismatch"
        ],
        "parent_type": "global-system-config",
        "id_perms": {
            "description": "There is a mismatch between installed and running package version."
        },
        "uve_keys": {
            "uve_key": [
                "analytics-node",
                "analytics-alarm-node",
                "analytics-snmp-node",
                "config-node",
                "control-node",
                "database-node",
                "config-database-node",
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "VrouterAgent.res_limit",
                            "operation": "==",
                            "operand2": {
                                "json_value": "true"
                            },
                            "variables": ["VrouterAgent.vr_limit_exceeded_list"]
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 1,
        "fq_name": [
            "default-global-system-config",
            "system-defined-vrouter-limit-exceeded"
        ],
        "id_perms": {
            "description": "Agent resource usage exceeded configured watermark for resource."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "vrouter"
            ]
        }
    },
    {
        "alarm_rules": {
            "or_list": [
                {
                    "and_list": [
                        {
                            "operand1": "VrouterAgent.res_table_limit",
                            "operation": "==",
                            "operand2": {
                                "json_value": "true"
                            },
                            "variables": ["VrouterAgent.vr_limit_exceeded_list"]
                        }
                    ]
                }
            ]
        },
        "alarm_severity": 0,
        "fq_name": [
            "default-global-system-config",
            "system-defined-vrouter-table-limit-exceeded"
        ],
        "id_perms": {
            "description": "Agent resource usage exceeded table size for resource in vrouter."
        },
        "parent_type": "global-system-config",
        "uve_keys": {
            "uve_key": [
                "vrouter"
            ]
        }
    },
]
