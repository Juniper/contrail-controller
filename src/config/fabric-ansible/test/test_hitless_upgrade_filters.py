#!/usr/bin/python
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
import gevent
import gevent.monkey
gevent.monkey.patch_all(thread=False)
import sys
import logging
from flexmock import flexmock
import json
from cfgm_common.exceptions import (
    RefsExistError
)

sys.path.append('../common/cfgm_common/tests')

from . import test_case
from test_utils import FakeKazooClient

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

sys.path.append('../fabric-ansible/ansible-playbooks/filter_plugins')

from hitless_upgrade_filters import FilterModule

sys.path.append('../fabric-ansible/ansible-playbooks/module_utils')

from job_manager import job_utils
from vnc_api.vnc_api import VncApi

from vnc_api.vnc_api import (
    Fabric,
    PhysicalRouter,
    PhysicalRole,
    PhysicalInterface,
    VirtualPortGroup,
    DeviceImage,
    JobTemplate,
    FabricNamespace,
    VirtualNetwork,
    NetworkIpam,
    LogicalInterface,
    InstanceIp,
    BgpRouter,
    LogicalRouter,
    IpamSubnets,
    IpamSubnetType,
    SubnetType,
    SerialNumListType,
    VnSubnetsType,
    VirtualNetworkType,
    FabricNetworkTag,
    NamespaceValue,
    RoutingBridgingRolesType,
    SubnetListType,
    KeyValuePairs,
    KeyValuePair,
    UserCredentials,
    DeviceCredentialList,
    DeviceCredential,
    VpgInterfaceParametersType
)

DGSC = "default-global-system-config"

FAB_UUID1 = "dfb0cd32-46ca-4996-b155-806878d4e500"
IMG_UUID1 = "dfb0cd32-46ca-4996-b155-806878d4e501"
IMG_UUID2 = "dfb0cd32-46ca-4996-b155-806878d4e502"
DEV_UUID1 = "dfb0cd32-46ca-4996-b155-806878d4e511"
DEV_UUID2 = "dfb0cd32-46ca-4996-b155-806878d4e512"
DEV_UUID3 = "dfb0cd32-46ca-4996-b155-806878d4e513"
DEV_UUID4 = "dfb0cd32-46ca-4996-b155-806878d4e514"
DEV_UUID5 = "dfb0cd32-46ca-4996-b155-806878d4e515"
DEV_UUID6 = "dfb0cd32-46ca-4996-b155-806878d4e516"
DEV_UUID7 = "dfb0cd32-46ca-4996-b155-806878d4e517"
DEV_UUID8 = "dfb0cd32-46ca-4996-b155-806878d4e518"
VPG_UUID1 = "dfb0cd32-46ca-4996-b155-806878d4e521"
VPG_UUID2 = "dfb0cd32-46ca-4996-b155-806878d4e522"
PI_UUID1  = "dfb0cd32-46ca-4996-b155-806878d4e531"
PI_UUID2  = "dfb0cd32-46ca-4996-b155-806878d4e532"
PI_UUID3  = "dfb0cd32-46ca-4996-b155-806878d4e533"
PI_UUID4  = "dfb0cd32-46ca-4996-b155-806878d4e534"
PI_UUID5  = "dfb0cd32-46ca-4996-b155-806878d4e535"
PI_UUID6  = "dfb0cd32-46ca-4996-b155-806878d4e536"
PI_UUID7  = "dfb0cd32-46ca-4996-b155-806878d4e537"
PI_UUID8  = "dfb0cd32-46ca-4996-b155-806878d4e538"


mock_job_input = {
    "image_devices": [],
    "upgrade_mode": "test_run",
    "fabric_uuid": FAB_UUID1,
    "advanced_parameters": {
        "bulk_device_upgrade_count": 5
    }
}

mock_job_ctx = {
    "fabric_fqname": "fab01",
    "job_template_fqname": [DGSC, "hitless_upgrade_strategy_template"],
    "job_input": mock_job_input,
    "vnc_api_init_params": {"admin_password": "c0ntrail123"}
}

mock_image_upgrade_list = [
    {
        "image_uuid": IMG_UUID1,
        "device_list": [DEV_UUID1, DEV_UUID4]
    },
    {
        "image_uuid": IMG_UUID2,
        "device_list": [DEV_UUID2, DEV_UUID3, DEV_UUID5, DEV_UUID6, DEV_UUID7, DEV_UUID8]
    }
]

mock_upgrade_plan = {
    "batch_idx": 0,
    "batches": [
        {
            "device_list": [
                DEV_UUID2,
                DEV_UUID5,
                DEV_UUID7,
                DEV_UUID8
            ],
            "device_names": [
                "device2",
                "device5",
                "device7",
                "device8"
            ],
            "name": "batch_1"
        },
        {
            "device_list": [
                DEV_UUID3,
                DEV_UUID6
            ],
            "device_names": [
                "device3",
                "device6"
            ],
            "name": "batch_2"
        },
        {
            "device_list": [
                DEV_UUID1
            ],
            "device_names": [
                "device1"
            ],
            "name": "batch_3"
        },
        {
            "device_list": [
                DEV_UUID4
            ],
            "device_names": [
                "device4"
            ],
            "name": "batch_4"
        }
    ],
    "device_table": {
        DEV_UUID1: {
            "basic": {
                "device_family": "qfx",
                "device_fqname": [
                    "device1"
                ],
                "device_management_ip": "1.1.1.1",
                "device_password": "contrail123",
                "device_product": "qfx-10000",
                "device_serial_number": "12345",
                "device_username": "root",
                "device_vendor": "juniper",
                "device_image_version": "mx_version_1",
            },
            "err_msgs": [],
            "image_family": "mx",
            "vpg_info": {
                "buddies": [],
                "vpg_list": []
            },
            "name": "device1",
            "physical_role": "spine",
            "rb_roles": [
                "DC-Gateway"
            ],
            "role": "DC-Gateway@spine",
            "uuid": DEV_UUID1,
            "batch_index": 2
        },
        DEV_UUID4: {
            "basic": {
                "device_family": "qfx",
                "device_fqname": [
                    "device4"
                ],
                "device_management_ip": "1.1.1.4",
                "device_password": "contrail123",
                "device_product": "qfx-10000",
                "device_serial_number": "12345",
                "device_username": "root",
                "device_vendor": "juniper",
                "device_image_version": "mx_version_1",
            },
            "err_msgs": [],
            "image_family": "mx",
            "vpg_info": {
                "buddies": [],
                "vpg_list": []
            },
            "name": "device4",
            "physical_role": "spine",
            "rb_roles": [
                "DC-Gateway"
            ],
            "role": "DC-Gateway@spine",
            "uuid": DEV_UUID4,
            "batch_index": 3
        },
        DEV_UUID2: {
            "basic": {
                "device_family": "qfx",
                "device_fqname": [
                    "device2"
                ],
                "device_management_ip": "1.1.1.2",
                "device_password": "contrail123",
                "device_product": "qfx-10000",
                "device_serial_number": "12345",
                "device_username": "root",
                "device_vendor": "juniper",
                "device_image_version": "mx_version_1",
            },
            "err_msgs": [],
            "image_family": "qfx",
            "vpg_info": {
                "buddies": [
                    DEV_UUID3
                ],
                "vpg_list": [
                    VPG_UUID1
                ]
            },
            "name": "device2",
            "physical_role": "leaf",
            "rb_roles": [
                "CRB-Access"
            ],
            "role": "CRB-Access@leaf",
            "uuid": DEV_UUID2,
            "batch_index": 0
        },
        DEV_UUID3: {
            "basic": {
                "device_family": "qfx",
                "device_fqname": [
                    "device3"
                ],
                "device_management_ip": "1.1.1.3",
                "device_password": "contrail123",
                "device_product": "qfx-10000",
                "device_serial_number": "12345",
                "device_username": "root",
                "device_vendor": "juniper",
                "device_image_version": "mx_version_1",
            },
            "err_msgs": [],
            "image_family": "qfx",
            "vpg_info": {
                "buddies": [
                    DEV_UUID2
                ],
                "vpg_list": [
                    VPG_UUID1
                ]
            },
            "name": "device3",
            "physical_role": "leaf",
            "rb_roles": [
                "CRB-Access"
            ],
            "role": "CRB-Access@leaf",
            "uuid": DEV_UUID3,
            "batch_index": 1
        },
        DEV_UUID5: {
            "basic": {
                "device_family": "qfx",
                "device_fqname": [
                    "device5"
                ],
                "device_management_ip": "1.1.1.5",
                "device_password": "contrail123",
                "device_product": "qfx-10000",
                "device_serial_number": "12345",
                "device_username": "root",
                "device_vendor": "juniper",
                "device_image_version": "mx_version_1",
            },
            "err_msgs": [],
            "image_family": "qfx",
            "vpg_info": {
                "buddies": [
                    DEV_UUID6
                ],
                "vpg_list": [
                    VPG_UUID2
                ]
            },
            "name": "device5",
            "physical_role": "leaf",
            "rb_roles": [
                "CRB-Access"
            ],
            "role": "CRB-Access@leaf",
            "uuid": DEV_UUID5,
            "batch_index": 0
        },
        DEV_UUID6: {
            "basic": {
                "device_family": "qfx",
                "device_fqname": [
                    "device6"
                ],
                "device_management_ip": "1.1.1.6",
                "device_password": "contrail123",
                "device_product": "qfx-10000",
                "device_serial_number": "12345",
                "device_username": "root",
                "device_vendor": "juniper",
                "device_image_version": "mx_version_1",
            },
            "err_msgs": [],
            "image_family": "qfx",
            "vpg_info": {
                "buddies": [
                    DEV_UUID5
                ],
                "vpg_list": [
                    VPG_UUID2
                ]
            },
            "name": "device6",
            "physical_role": "leaf",
            "rb_roles": [
                "CRB-Access"
            ],
            "role": "CRB-Access@leaf",
            "uuid": DEV_UUID6,
            "batch_index": 1
        },
        DEV_UUID7: {
            "basic": {
                "device_family": "qfx",
                "device_fqname": [
                    "device7"
                ],
                "device_management_ip": "1.1.1.7",
                "device_password": "contrail123",
                "device_product": "qfx-10000",
                "device_serial_number": "12345",
                "device_username": "root",
                "device_vendor": "juniper",
                "device_image_version": "mx_version_1",
            },
            "err_msgs": [],
            "image_family": "qfx",
            "vpg_info": {
                "buddies": [],
                "vpg_list": []
            },
            "name": "device7",
            "physical_role": "leaf",
            "rb_roles": [
                "CRB-Access"
            ],
            "role": "CRB-Access@leaf",
            "uuid": DEV_UUID7,
            "batch_index": 0
        },
        DEV_UUID8: {
            "basic": {
                "device_family": "qfx",
                "device_fqname": [
                    "device8"
                ],
                "device_management_ip": "1.1.1.8",
                "device_password": "contrail123",
                "device_product": "qfx-10000",
                "device_serial_number": "12345",
                "device_username": "root",
                "device_vendor": "juniper",
                "device_image_version": "mx_version_1",
            },
            "err_msgs": [],
            "image_family": "qfx",
            "vpg_info": {
                "buddies": [],
                "vpg_list": []
            },
            "name": "device8",
            "physical_role": "leaf",
            "rb_roles": [
                "CRB-Access"
            ],
            "role": "CRB-Access@leaf",
            "uuid": DEV_UUID8,
            "batch_index": 0
        }
    }
}

mock_device_image_db = {
    IMG_UUID1: {
        "name": "Image1",
        "device_image_os_version": "17.1.2",
        "device_image_device_family": "mx",
    },
    IMG_UUID2: {
        "name": "Image2",
        "device_image_os_version": "17.1.2",
        "device_image_device_family": "qfx",
    },
}
mock_physical_router_db = {
    DEV_UUID1: {
        "display_name": "device_1",
        "physical_router_role": "spine",
        "routing_bridging_roles": ["DC-Gateway"],
        "fq_name": [DGSC, "device_1"],
        "physical_router_vendor_name": "juniper",
        "physical_router_device_family": "qfx",
        "physical_router_product_name": "qfx-10000",
        "physical_router_serial_number": "12345",
        "physical_router_management_ip": "1.1.1.1",
        "physical_router_os_version": "17.1.1",
        "device_username": "root",
        "device_password": "contrail123",
    },
    DEV_UUID2: {
        "display_name": "device_2",
        "physical_router_role": "leaf",
        "routing_bridging_roles": ["CRB-Access"],
        "fq_name": [DGSC, "device_2"],
        "physical_router_vendor_name": "juniper",
        "physical_router_device_family": "qfx",
        "physical_router_product_name": "qfx-10000",
        "physical_router_serial_number": "12345",
        "physical_router_management_ip": "1.1.1.2",
        "physical_router_os_version": "17.1.1",
        "device_username": "root",
        "device_password": "contrail123",
    },
    DEV_UUID3: {
        "display_name": "device_3",
        "physical_router_role": "leaf",
        "routing_bridging_roles": ["CRB-Access"],
        "fq_name": [DGSC, "device_3"],
        "physical_router_vendor_name": "juniper",
        "physical_router_device_family": "qfx",
        "physical_router_product_name": "qfx-10000",
        "physical_router_serial_number": "12345",
        "physical_router_management_ip": "1.1.1.3",
        "physical_router_os_version": "17.1.1",
        "device_username": "root",
        "device_password": "contrail123",
    },
    DEV_UUID4: {
        "display_name": "device_4",
        "physical_router_role": "spine",
        "routing_bridging_roles": ["DC-Gateway"],
        "fq_name": [DGSC, "device_4"],
        "physical_router_vendor_name": "juniper",
        "physical_router_device_family": "qfx",
        "physical_router_product_name": "qfx-10000",
        "physical_router_serial_number": "12345",
        "physical_router_management_ip": "1.1.1.4",
        "physical_router_os_version": "17.1.1",
        "device_username": "root",
        "device_password": "contrail123",
    },
    DEV_UUID5: {
        "display_name": "device_5",
        "physical_router_role": "leaf",
        "routing_bridging_roles": ["CRB-Access"],
        "fq_name": [DGSC, "device_5"],
        "physical_router_vendor_name": "juniper",
        "physical_router_device_family": "qfx",
        "physical_router_product_name": "qfx-10000",
        "physical_router_serial_number": "12345",
        "physical_router_management_ip": "1.1.1.5",
        "physical_router_os_version": "17.1.1",
        "device_username": "root",
        "device_password": "contrail123",
    },
    DEV_UUID6: {
        "display_name": "device_6",
        "physical_router_role": "leaf",
        "routing_bridging_roles": ["CRB-Access"],
        "fq_name": [DGSC, "device_6"],
        "physical_router_vendor_name": "juniper",
        "physical_router_device_family": "qfx",
        "physical_router_product_name": "qfx-10000",
        "physical_router_serial_number": "12345",
        "physical_router_management_ip": "1.1.1.6",
        "physical_router_os_version": "17.1.1",
        "device_username": "root",
        "device_password": "contrail123",
    },
    DEV_UUID7: {
        "display_name": "device_7",
        "physical_router_role": "leaf",
        "routing_bridging_roles": ["CRB-Access"],
        "fq_name": [DGSC, "device_7"],
        "physical_router_vendor_name": "juniper",
        "physical_router_device_family": "qfx",
        "physical_router_product_name": "qfx-10000",
        "physical_router_serial_number": "12345",
        "physical_router_management_ip": "1.1.1.7",
        "physical_router_os_version": "17.1.1",
        "device_username": "root",
        "device_password": "contrail123",
    },
    DEV_UUID8: {
        "display_name": "device_8",
        "physical_router_role": "leaf",
        "routing_bridging_roles": ["CRB-Access"],
        "fq_name": [DGSC, "device_8"],
        "physical_router_vendor_name": "juniper",
        "physical_router_device_family": "qfx",
        "physical_router_product_name": "qfx-10000",
        "physical_router_serial_number": "12345",
        "physical_router_management_ip": "1.1.1.8",
        "physical_router_os_version": "17.1.1",
        "device_username": "root",
        "device_password": "contrail123",
    },
}

mock_physical_interface_db = {
    PI_UUID1: {
        "parent_uuid": DEV_UUID2,
        "fq_name": [DGSC, "device_2","pi_1"],
        "uuid": PI_UUID1
    },
    PI_UUID2: {
        "parent_uuid": DEV_UUID3,
        "fq_name": [DGSC, "device_3","pi_2"],
        "uuid": PI_UUID2
    },
    PI_UUID3: {
        "parent_uuid": DEV_UUID5,
        "fq_name": [DGSC, "device_5","pi_3"],
        "uuid": PI_UUID3
    },
    PI_UUID4: {
        "parent_uuid": DEV_UUID6,
        "fq_name": [DGSC, "device_6","pi_4"],
        "uuid": PI_UUID4
    },
    PI_UUID5: {
        "parent_uuid": DEV_UUID7,
        "fq_name": [DGSC, "device_7","pi_5"],
        "uuid": PI_UUID5
    },
    PI_UUID6: {
        "parent_uuid": DEV_UUID8,
        "fq_name": [DGSC, "device_8","pi_6"],
        "uuid": PI_UUID6
    },
}

mock_virtual_port_group_db = {
    VPG_UUID1: {
        "name": "vpg_1",
        "fq_name": [DGSC, "fab01", "vpg_1"],
        "refs": [
            {"uuid": PI_UUID1},
            {"uuid": PI_UUID2},
        ]
    },
    VPG_UUID2: {
        "name": "vpg_2",
        "fq_name": [DGSC, "fab01", "vpg_2"],
        "refs": [
            {"uuid": PI_UUID3},
            {"uuid": PI_UUID4},
        ]
    }
}

mock_job_template_input_schema = """{
    "$schema": "http://json-schema.org/draft-06/schema#",
    "additionalProperties": false,
    "properties": {
        "advanced_parameters": {
            "default": {},
            "additionalProperties": false,
            "description": "Optional parameters used to override defaults",
            "properties": {
                "active_route_count_check": {
                    "default": true,
                    "description": "Enable/disable active route count check",
                    "type": "boolean"
                },
                "alarm": {
                    "default": {},
                    "additionalProperties": false,
                    "properties": {
                        "chassis_alarm_check": {
                            "default": true,
                            "description": "Enable/disable chassis alarm check",
                            "type": "boolean"
                        },
                        "system_alarm_check": {
                            "default": true,
                            "description": "Enable/disable system alarm check",
                            "type": "boolean"
                        }
                    },
                    "type": "object"
                },
                "bgp": {
                    "default": {},
                    "additionalProperties": false,
                    "properties": {
                        "bgp_down_peer_count": {
                            "default": 0,
                            "description": "Number of down peers allowed",
                            "type": "integer"
                        },
                        "bgp_down_peer_count_check": {
                            "default": true,
                            "description": "Enable/disable bgp_down_peer_count check",
                            "type": "boolean"
                        },
                        "bgp_flap_count": {
                            "default": 4,
                            "description": "Number of flaps allowed for BGP neighbors",
                            "type": "integer"
                        },
                        "bgp_flap_count_check": {
                            "default": true,
                            "description": "Enable/disable bgp_flap_count check",
                            "type": "boolean"
                        },
                        "bgp_peer_state_check": {
                            "default": true,
                            "description": "Enable/disable bgp peer state check",
                            "type": "boolean"
                        }
                    },
                    "type": "object"
                },
                "bulk_device_upgrade_count": {
                    "default": 4,
                    "description": "Maximum number of devices to upgrade simultaneously",
                    "type": "integer"
                },
                "fpc": {
                    "default": {},
                    "additionalProperties": false,
                    "properties": {
                        "fpc_cpu_5min_avg": {
                            "default": 50,
                            "description": "FPC CP5 minute average utilization",
                            "type": "integer"
                        },
                        "fpc_cpu_5min_avg_check": {
                            "default": true,
                            "description": "Enable/disable FPC CP5 minute average utilizationcheck",
                            "type": "boolean"
                        },
                        "fpc_memory_heap_util": {
                            "default": 45,
                            "description": "FPC memory heap utilization",
                            "type": "integer"
                        },
                        "fpc_memory_heap_util_check": {
                            "default": true,
                            "description": "Enable/disable FPC memory heap utilization check",
                            "type": "boolean"
                        }
                    },
                    "type": "object"
                },
                "interface": {
                    "default": {},
                    "additionalProperties": false,
                    "properties": {
                        "interface_carrier_transition_count_check": {
                            "default": true,
                            "description": "Enable/disable interface carrier transition check",
                            "type": "boolean"
                        },
                        "interface_drop_count_check": {
                            "default": true,
                            "description": "Enable/disable interface drop check",
                            "type": "boolean"
                        },
                        "interface_error_check": {
                            "default": true,
                            "description": "Enable/disable interface error check",
                            "type": "boolean"
                        }
                    },
                    "type": "object"
                },
                "l2_total_mac_count_check": {
                    "default": true,
                    "description": "Enable/disable l2 total mac count check",
                    "type": "boolean"
                },
                "routing_engine": {
                    "default": {},
                    "additionalProperties": false,
                    "properties": {
                        "routing_engine_cpu_idle": {
                            "default": 60,
                            "description": "Routing engine CPidle time",
                            "type": "integer"
                        },
                        "routing_engine_cpu_idle_check": {
                            "default": true,
                            "description": "Enable/disable routing engine CLidle time check",
                            "type": "boolean"
                        }
                    },
                    "type": "object"
                },
                "storm_control_fvpg_check": {
                    "default": true,
                    "description": "Enable/disable storm control fvpg check",
                    "type": "boolean"
                }
            },
            "title": "Advanced parameters",
            "type": "object"
        }
    },
    "title": "Image upgrade strategy input",
    "type": "object"
}"""

mock_job_templates_list = {
    "job-templates": [
        {
            "job-template": {
                "fq_name": [
                    "default-global-system-config",
                    "hitless_upgrade_strategy_template"
                ],
                "href": "http://192.168.3.23:8082/job-template/c5af95a8-5268-48f0-8a60-1826ae51f8f1",
                "id_perms": {
                    "created": "2018-12-12T07:03:57.349095",
                    "creator": "",
                    "description": "",
                    "enable": True,
                    "last_modified": "2019-01-10T22:02:39.012954",
                    "permissions": {
                        "group": "cloud-admin-group",
                        "group_access": 7,
                        "other_access": 7,
                        "owner": "cloud-admin",
                        "owner_access": 7
                    },
                    "user_visible": True,
                    "uuid": {
                        "uuid_lslong": 9970996129410709745,
                        "uuid_mslong": 14244768696565778672
                    }
                },
                "job_template_concurrency_level": "fabric",
                "job_template_input_schema": "{\"additionalProperties\": false, \"$schema\": \"http://json-schema.org/draft-06/schema#\", \"type\": \"object\", \"properties\": {\"image_devices\": {\"items\": {\"description\": \"List of images and corresponding devices to upgrade\", \"title\": \"Image Devices\", \"required\": [\"image_uuid\", \"device_list\"], \"additionalProperties\": false, \"type\": \"object\", \"properties\": {\"image_uuid\": {\"type\": \"string\"}, \"device_list\": {\"items\": {\"format\": \"uuid\", \"type\": \"string\", \"title\": \"device uuid\"}, \"type\": \"array\", \"description\": \"List of device UUIDs to be upgraded with this image\"}}}, \"type\": \"array\"}, \"advanced_parameters\": {\"description\": \"Optional parameters used to override defaults\", \"title\": \"Advanced parameters\", \"default\": {}, \"additionalProperties\": false, \"type\": \"object\", \"properties\": {\"bulk_device_upgrade_count\": {\"default\": 4, \"type\": \"integer\", \"description\": \"Maximum number of devices to upgrade simultaneously\"}, \"health_check_abort\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable abort upon health check failures\"}, \"Juniper\": {\"additionalProperties\": false, \"default\": {}, \"type\": \"object\", \"properties\": {\"lacp\": {\"additionalProperties\": false, \"default\": {}, \"type\": \"object\", \"properties\": {\"lacp_down_local_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable local interface down check\"}, \"lacp_down_peer_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable peer interface down check\"}}}, \"storm_control_flag_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable storm control flag check\"}, \"l2_total_mac_count_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable l2 total mac count check\"}, \"active_route_count_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable active route count check\"}, \"fpc\": {\"additionalProperties\": false, \"default\": {}, \"type\": \"object\", \"properties\": {\"fpc_cpu_5min_avg_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable FPC CPU 5 minute average utilizationcheck\"}, \"fpc_cpu_5min_avg\": {\"default\": 50, \"type\": \"integer\", \"description\": \"FPC CPU 5 minute average utilization\"}, \"fpc_memory_heap_util\": {\"default\": 45, \"type\": \"integer\", \"description\": \"FPC memory heap utilization\"}, \"fpc_memory_heap_util_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable FPC memory heap utilization check\"}}}, \"alarm\": {\"additionalProperties\": false, \"default\": {}, \"type\": \"object\", \"properties\": {\"system_alarm_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable system alarm check\"}, \"chassis_alarm_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable chassis alarm check\"}}}, \"bgp\": {\"additionalProperties\": false, \"default\": {}, \"type\": \"object\", \"properties\": {\"bgp_down_peer_count_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable bgp_down_peer_count check\"}, \"bgp_flap_count_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable bgp_flap_count check\"}, \"bgp_flap_count\": {\"default\": 4, \"type\": \"integer\", \"description\": \"Number of flaps allowed for BGP neighbors\"}, \"bgp_down_peer_count\": {\"default\": 0, \"type\": \"integer\", \"description\": \"Number of down peers allowed\"}, \"bgp_peer_state_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable bgp peer state check\"}}}, \"interface\": {\"additionalProperties\": false, \"default\": {}, \"type\": \"object\", \"properties\": {\"interface_drop_count_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable interface drop check\"}, \"interface_error_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable interface error check\"}, \"interface_carrier_transition_count_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable interface carrier transition check\"}}}, \"routing_engine\": {\"additionalProperties\": false, \"default\": {}, \"type\": \"object\", \"properties\": {\"routing_engine_cpu_idle_check\": {\"default\": true, \"type\": \"boolean\", \"description\": \"Enable/disable routing engine CLU idle time check\"}, \"routing_engine_cpu_idle\": {\"default\": 60, \"type\": \"integer\", \"description\": \"Routing engine CPU idle time\"}}}}}}}, \"fabric_uuid\": {\"type\": \"string\", \"description\": \"Fabric UUID\"}, \"device_list\": {\"items\": {\"format\": \"uuid\", \"type\": \"string\", \"title\": \"device uuid\"}, \"type\": \"array\", \"description\": \"List of device UUIDs to be upgraded\"}, \"upgrade_mode\": {\"enum\": [\"test_run\", \"dry_run\", \"upgrade\"], \"description\": \"Mode in which to run workflow\"}}, \"title\": \"Image upgrade strategy input\"}",
                "job_template_multi_device_job": False,
                'job_template_output_schema': {
                    "$schema": "http://json-schema.org/draft-06/schema#",
                    "properties": {
                        "message": {
                            "description": "Should capture a summarized error message in case of Failures.",
                            "type": "string"
                        },
                        "results": {
                            "description": "JSON object holding the job specific output details",
                            "type": "object"
                        },
                        "status": {
                            "description": "Result status of the job",
                            "enum": [
                                "Success",
                                "Failure",
                                "Timeout"
                            ],
                            "type": "string"
                        }
                    },
                    "required": [
                        "status"
                    ],
                    "title": "Hitless image upgrade strategy output",
                    "type": "object"
                },
                "job_template_playbooks": {
                    "playbook_info": [
                        {
                            "device_family": "",
                            "job_completion_weightage": 10,
                            "playbook_uri": "./opt/contrail/fabric_ansible_playbooks/hitless_upgrade_strategy.yml",
                            "vendor": "Juniper"
                        },
                        {
                            "device_family": "",
                            "job_completion_weightage": 90,
                            "playbook_uri": "./opt/contrail/fabric_ansible_playbooks/hitless_upgrade.yml",
                            "vendor": "Juniper"
                        }
                    ]
                },
                "name": "hitless_upgrade_strategy_template",
                "parent_type": "global-system-config",
                "parent_uuid": "21608fd9-b496-4a03-965f-a42662e947ca",
                "perms2": {
                    "global_access": 0,
                    "owner": "cloud-admin",
                    "owner_access": 7,
                    "share": []
                },
                "uuid": "c5af95a8-5268-48f0-8a60-1826ae51f8f1"
            }
        }
    ]
}

mock_upgrade_plan_result = {
    'status': 'success',
    'results': "\n********** Summary *************\n\nTotal estimated duration is 2:00:00.\n\nNote that this time estimate may vary depending on network speeds and system capabilities.\nThe following batches of devices will be upgraded in the order listed:\n\nBatch 1:\n  device_2  17.1.1 --> 17.1.2  \n  device_5  17.1.1 --> 17.1.2  \n  device_7  17.1.1 --> 17.1.2  \n  device_8  17.1.1 --> 17.1.2  \n\nBatch 2:\n  device_3  17.1.1 --> 17.1.2  \n  device_6  17.1.1 --> 17.1.2  \n\nBatch 3:\n  device_1  17.1.1 --> 17.1.2  \n\nBatch 4:\n  device_4  17.1.1 --> 17.1.2  \n\n\n******** Details ************\n\nDetailed information for the devices to be upgraded is listed below:\n\n  - device_1\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e511\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.1\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : mx\n    physical role    : spine\n    routing bridging roles: ['DC-Gateway']\n    role             : spine\n    vpg list         : []\n    vpg peers        : []\n    batch            : Batch 3\n    is hitless?      : True\n\n  - device_2\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e512\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.2\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : ['dfb0cd32-46ca-4996-b155-806878d4e521']\n    vpg peers        : ['dfb0cd32-46ca-4996-b155-806878d4e513']\n    batch            : Batch 1\n    is hitless?      : True\n\n  - device_3\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e513\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.3\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : ['dfb0cd32-46ca-4996-b155-806878d4e521']\n    vpg peers        : ['dfb0cd32-46ca-4996-b155-806878d4e512']\n    batch            : Batch 2\n    is hitless?      : True\n\n  - device_4\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e514\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.4\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : mx\n    physical role    : spine\n    routing bridging roles: ['DC-Gateway']\n    role             : spine\n    vpg list         : []\n    vpg peers        : []\n    batch            : Batch 4\n    is hitless?      : True\n\n  - device_5\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e515\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.5\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : ['dfb0cd32-46ca-4996-b155-806878d4e522']\n    vpg peers        : ['dfb0cd32-46ca-4996-b155-806878d4e516']\n    batch            : Batch 1\n    is hitless?      : True\n\n  - device_6\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e516\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.6\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : ['dfb0cd32-46ca-4996-b155-806878d4e522']\n    vpg peers        : ['dfb0cd32-46ca-4996-b155-806878d4e515']\n    batch            : Batch 2\n    is hitless?      : True\n\n  - device_7\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e517\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.7\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : []\n    vpg peers        : []\n    batch            : Batch 1\n    is hitless?      : True\n\n  - device_8\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e518\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.8\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : []\n    vpg peers        : []\n    batch            : Batch 1\n    is hitless?      : True\n",
    'vpg_table': {
        'dfb0cd32-46ca-4996-b155-806878d4e521': {
            'name': 'vpg_1',
            'device_table': {
                'dfb0cd32-46ca-4996-b155-806878d4e513': [{
                    'fq_name': ['default-global-system-config', 'device_3',
                                'pi_2'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e532'
                }],
                'dfb0cd32-46ca-4996-b155-806878d4e512': [{
                    'fq_name': ['default-global-system-config', 'device_2',
                                'pi_1'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e531'
                }]
            }
        },
        'dfb0cd32-46ca-4996-b155-806878d4e522': {
            'name': 'vpg_2',
            'device_table': {
                'dfb0cd32-46ca-4996-b155-806878d4e515': [{
                    'fq_name': ['default-global-system-config', 'device_5',
                                'pi_3'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e533'
                }],
                'dfb0cd32-46ca-4996-b155-806878d4e516': [{
                    'fq_name': ['default-global-system-config', 'device_6',
                                'pi_4'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e534'
                }]
            }
        }
    },
    'report': "\n********** Summary *************\n\nTotal estimated duration is 2:00:00.\n\nNote that this time estimate may vary depending on network speeds and system capabilities.\nThe following batches of devices will be upgraded in the order listed:\n\nBatch 1:\n  device_2  17.1.1 --> 17.1.2  \n  device_5  17.1.1 --> 17.1.2  \n  device_7  17.1.1 --> 17.1.2  \n  device_8  17.1.1 --> 17.1.2  \n\nBatch 2:\n  device_3  17.1.1 --> 17.1.2  \n  device_6  17.1.1 --> 17.1.2  \n\nBatch 3:\n  device_1  17.1.1 --> 17.1.2  \n\nBatch 4:\n  device_4  17.1.1 --> 17.1.2  \n\n\n******** Details ************\n\nDetailed information for the devices to be upgraded is listed below:\n\n  - device_1\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e511\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.1\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : mx\n    physical role    : spine\n    routing bridging roles: ['DC-Gateway']\n    role             : spine\n    vpg list         : []\n    vpg peers        : []\n    batch            : Batch 3\n    is hitless?      : True\n\n  - device_2\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e512\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.2\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : ['dfb0cd32-46ca-4996-b155-806878d4e521']\n    vpg peers        : ['dfb0cd32-46ca-4996-b155-806878d4e513']\n    batch            : Batch 1\n    is hitless?      : True\n\n  - device_3\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e513\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.3\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : ['dfb0cd32-46ca-4996-b155-806878d4e521']\n    vpg peers        : ['dfb0cd32-46ca-4996-b155-806878d4e512']\n    batch            : Batch 2\n    is hitless?      : True\n\n  - device_4\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e514\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.4\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : mx\n    physical role    : spine\n    routing bridging roles: ['DC-Gateway']\n    role             : spine\n    vpg list         : []\n    vpg peers        : []\n    batch            : Batch 4\n    is hitless?      : True\n\n  - device_5\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e515\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.5\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : ['dfb0cd32-46ca-4996-b155-806878d4e522']\n    vpg peers        : ['dfb0cd32-46ca-4996-b155-806878d4e516']\n    batch            : Batch 1\n    is hitless?      : True\n\n  - device_6\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e516\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.6\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : ['dfb0cd32-46ca-4996-b155-806878d4e522']\n    vpg peers        : ['dfb0cd32-46ca-4996-b155-806878d4e515']\n    batch            : Batch 2\n    is hitless?      : True\n\n  - device_7\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e517\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.7\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : []\n    vpg peers        : []\n    batch            : Batch 1\n    is hitless?      : True\n\n  - device_8\n    uuid             : dfb0cd32-46ca-4996-b155-806878d4e518\n    vendor           : juniper\n    family           : qfx\n    product          : qfx-10000\n    serial number    : 12345\n    management ip    : 1.1.1.8\n    username         : root\n    password         : ** hidden **\n    new image version: 17.1.2\n    current image version: 17.1.1\n    image family     : qfx\n    physical role    : leaf\n    routing bridging roles: ['CRB-Access']\n    role             : leaf\n    vpg list         : []\n    vpg peers        : []\n    batch            : Batch 1\n    is hitless?      : True\n",
    'role_device_groups': {
        'spine': ['dfb0cd32-46ca-4996-b155-806878d4e511',
                  'dfb0cd32-46ca-4996-b155-806878d4e514'],
        'leaf': ['dfb0cd32-46ca-4996-b155-806878d4e512',
                 'dfb0cd32-46ca-4996-b155-806878d4e513',
                 'dfb0cd32-46ca-4996-b155-806878d4e515',
                 'dfb0cd32-46ca-4996-b155-806878d4e516',
                 'dfb0cd32-46ca-4996-b155-806878d4e517',
                 'dfb0cd32-46ca-4996-b155-806878d4e518']
    },
    'skipped_device_table': {},
    'batches': [{
        'device_names': ['device_2', 'device_5', 'device_7', 'device_8'],
        'name': 'Batch 1',
        'device_list': ['dfb0cd32-46ca-4996-b155-806878d4e512',
                        'dfb0cd32-46ca-4996-b155-806878d4e515',
                        'dfb0cd32-46ca-4996-b155-806878d4e517',
                        'dfb0cd32-46ca-4996-b155-806878d4e518']
    }, {
        'device_names': ['device_3', 'device_6'],
        'name': 'Batch 2',
        'device_list': ['dfb0cd32-46ca-4996-b155-806878d4e513',
                        'dfb0cd32-46ca-4996-b155-806878d4e516']
    }, {
        'device_names': ['device_1'],
        'name': 'Batch 3',
        'device_list': ['dfb0cd32-46ca-4996-b155-806878d4e511']
    }, {
        'device_names': ['device_4'],
        'name': 'Batch 4',
        'device_list': ['dfb0cd32-46ca-4996-b155-806878d4e514']
    }],
    'device_table': {
        'dfb0cd32-46ca-4996-b155-806878d4e511': {
            'batch_index': 2,
            'image_family': 'mx',
            'current_image_version': '17.1.1',
            'target_multihomed_interface': [],
            'physical_role': 'spine',
            'image_version': '17.1.2',
            'name': 'device_1',
            'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e511',
            'rb_roles': ['DC-Gateway'],
            'vpg_info': {
                'buddies': [],
                'vpg_list': []
            },
            'role': 'spine',
            'basic': {
                'device_family': 'qfx',
                'device_fqname': ['default-global-system-config', 'device_1'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_uuid': 'dfb0cd32-46ca-4996-b155-806878d4e501',
                'device_hitless_upgrade': True,
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.1',
                'device_username': 'root',
                'device_password': '***'
            },
            'err_msgs': []
        },
        'dfb0cd32-46ca-4996-b155-806878d4e514': {
            'batch_index': 3,
            'image_family': 'mx',
            'current_image_version': '17.1.1',
            'target_multihomed_interface': [],
            'physical_role': 'spine',
            'image_version': '17.1.2',
            'name': 'device_4',
            'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e514',
            'rb_roles': ['DC-Gateway'],
            'vpg_info': {
                'buddies': [],
                'vpg_list': []
            },
            'role': 'spine',
            'basic': {
                'device_family': 'qfx',
                'device_fqname': ['default-global-system-config', 'device_4'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_uuid': 'dfb0cd32-46ca-4996-b155-806878d4e501',
                'device_hitless_upgrade': True,
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.4',
                'device_username': 'root',
                'device_password': '***'
            },
            'err_msgs': []
        },
        'dfb0cd32-46ca-4996-b155-806878d4e512': {
            'batch_index': 0,
            'image_family': 'qfx',
            'current_image_version': '17.1.1',
            'target_multihomed_interface': ['pi_1'],
            'physical_role': 'leaf',
            'image_version': '17.1.2',
            'name': 'device_2',
            'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e512',
            'rb_roles': ['CRB-Access'],
            'vpg_info': {
                'buddies': [{
                    'username': 'root',
                    'fq_name': ['default-global-system-config', 'device_3'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e513',
                    'mgmt_ip': '1.1.1.3',
                    'password': '***',
                    'vendor': 'juniper',
                    'multihomed_interface_list': ['pi_2'],
                    'name': 'device_3'
                }],
                'vpg_list': ['dfb0cd32-46ca-4996-b155-806878d4e521']
            },
            'role': 'leaf',
            'basic': {
                'device_family': 'qfx',
                'device_fqname': ['default-global-system-config', 'device_2'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_uuid': 'dfb0cd32-46ca-4996-b155-806878d4e502',
                'device_hitless_upgrade': True,
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.2',
                'device_username': 'root',
                'device_password': '***'
            },
            'err_msgs': []
        },
        'dfb0cd32-46ca-4996-b155-806878d4e513': {
            'batch_index': 1,
            'image_family': 'qfx',
            'current_image_version': '17.1.1',
            'target_multihomed_interface': ['pi_2'],
            'physical_role': 'leaf',
            'image_version': '17.1.2',
            'name': 'device_3',
            'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e513',
            'rb_roles': ['CRB-Access'],
            'vpg_info': {
                'buddies': [{
                    'username': 'root',
                    'fq_name': ['default-global-system-config', 'device_2'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e512',
                    'mgmt_ip': '1.1.1.2',
                    'password': '***',
                    'vendor': 'juniper',
                    'multihomed_interface_list': ['pi_1'],
                    'name': 'device_2'
                }],
                'vpg_list': ['dfb0cd32-46ca-4996-b155-806878d4e521']
            },
            'role': 'leaf',
            'basic': {
                'device_family': 'qfx',
                'device_fqname': ['default-global-system-config', 'device_3'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_uuid': 'dfb0cd32-46ca-4996-b155-806878d4e502',
                'device_hitless_upgrade': True,
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.3',
                'device_username': 'root',
                'device_password': '***'
            },
            'err_msgs': []
        },
        'dfb0cd32-46ca-4996-b155-806878d4e515': {
            'batch_index': 0,
            'image_family': 'qfx',
            'current_image_version': '17.1.1',
            'target_multihomed_interface': ['pi_3'],
            'physical_role': 'leaf',
            'image_version': '17.1.2',
            'name': 'device_5',
            'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e515',
            'rb_roles': ['CRB-Access'],
            'vpg_info': {
                'buddies': [{
                    'username': 'root',
                    'fq_name': ['default-global-system-config', 'device_6'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e516',
                    'mgmt_ip': '1.1.1.6',
                    'password': '***',
                    'vendor': 'juniper',
                    'multihomed_interface_list': ['pi_4'],
                    'name': 'device_6'
                }],
                'vpg_list': ['dfb0cd32-46ca-4996-b155-806878d4e522']
            },
            'role': 'leaf',
            'basic': {
                'device_family': 'qfx',
                'device_fqname': ['default-global-system-config', 'device_5'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_uuid': 'dfb0cd32-46ca-4996-b155-806878d4e502',
                'device_hitless_upgrade': True,
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.5',
                'device_username': 'root',
                'device_password': '***'
            },
            'err_msgs': []
        },
        'dfb0cd32-46ca-4996-b155-806878d4e516': {
            'batch_index': 1,
            'image_family': 'qfx',
            'current_image_version': '17.1.1',
            'target_multihomed_interface': ['pi_4'],
            'physical_role': 'leaf',
            'image_version': '17.1.2',
            'name': 'device_6',
            'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e516',
            'rb_roles': ['CRB-Access'],
            'vpg_info': {
                'buddies': [{
                    'username': 'root',
                    'fq_name': ['default-global-system-config', 'device_5'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e515',
                    'mgmt_ip': '1.1.1.5',
                    'password': '***',
                    'vendor': 'juniper',
                    'multihomed_interface_list': ['pi_3'],
                    'name': 'device_5'
                }],
                'vpg_list': ['dfb0cd32-46ca-4996-b155-806878d4e522']
            },
            'role': 'leaf',
            'basic': {
                'device_family': 'qfx',
                'device_fqname': ['default-global-system-config', 'device_6'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_uuid': 'dfb0cd32-46ca-4996-b155-806878d4e502',
                'device_hitless_upgrade': True,
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.6',
                'device_username': 'root',
                'device_password': '***'
            },
            'err_msgs': []
        },
        'dfb0cd32-46ca-4996-b155-806878d4e517': {
            'batch_index': 0,
            'image_family': 'qfx',
            'current_image_version': '17.1.1',
            'target_multihomed_interface': [],
            'physical_role': 'leaf',
            'image_version': '17.1.2',
            'name': 'device_7',
            'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e517',
            'rb_roles': ['CRB-Access'],
            'vpg_info': {
                'buddies': [],
                'vpg_list': []
            },
            'role': 'leaf',
            'basic': {
                'device_family': 'qfx',
                'device_fqname': ['default-global-system-config', 'device_7'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_uuid': 'dfb0cd32-46ca-4996-b155-806878d4e502',
                'device_hitless_upgrade': True,
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.7',
                'device_username': 'root',
                'device_password': '***'
            },
            'err_msgs': []
        },
        'dfb0cd32-46ca-4996-b155-806878d4e518': {
            'batch_index': 0,
            'image_family': 'qfx',
            'current_image_version': '17.1.1',
            'target_multihomed_interface': [],
            'physical_role': 'leaf',
            'image_version': '17.1.2',
            'name': 'device_8',
            'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e518',
            'rb_roles': ['CRB-Access'],
            'vpg_info': {
                'buddies': [],
                'vpg_list': []
            },
            'role': 'leaf',
            'basic': {
                'device_family': 'qfx',
                'device_fqname': ['default-global-system-config', 'device_8'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_uuid': 'dfb0cd32-46ca-4996-b155-806878d4e502',
                'device_hitless_upgrade': True,
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.8',
                'device_username': 'root',
                'device_password': '***'
            },
            'err_msgs': []
        }
    },
    'device_count': 8,
    'image_upgrade_list': [{
        'image_uuid': 'dfb0cd32-46ca-4996-b155-806878d4e501',
        'device_list': ['dfb0cd32-46ca-4996-b155-806878d4e511',
                        'dfb0cd32-46ca-4996-b155-806878d4e514']
    }, {
        'image_uuid': 'dfb0cd32-46ca-4996-b155-806878d4e502',
        'device_list': ['dfb0cd32-46ca-4996-b155-806878d4e512',
                        'dfb0cd32-46ca-4996-b155-806878d4e513',
                        'dfb0cd32-46ca-4996-b155-806878d4e515',
                        'dfb0cd32-46ca-4996-b155-806878d4e516',
                        'dfb0cd32-46ca-4996-b155-806878d4e517',
                        'dfb0cd32-46ca-4996-b155-806878d4e518']
    }],
    'advanced_parameters': {
        'health_check_abort': True,
        'bulk_device_upgrade_count': 5,
        'Juniper': {
            'storm_control_flag_check': True,
            'l2_total_mac_count_check': True,
            'active_route_count_check': True,
            'fpc': {
                'fpc_cpu_5min_avg_check': True,
                'fpc_cpu_5min_avg': 50,
                'fpc_memory_heap_util': 45,
                'fpc_memory_heap_util_check': True
            },
            'alarm': {
                'system_alarm_check': True,
                'chassis_alarm_check': True
            },
            'bgp': {
                'bgp_down_peer_count_check': True,
                'bgp_flap_count_check': True,
                'bgp_flap_count': 4,
                'bgp_down_peer_count': 0,
                'bgp_peer_state_check': True
            },
            'interface': {
                'interface_drop_count_check': True,
                'interface_error_check': True,
                'interface_carrier_transition_count_check': True
            },
            'lacp': {
                'lacp_down_local_check': True,
                'lacp_down_peer_check': True
            },
            'routing_engine': {
                'routing_engine_cpu_idle_check': True,
                'routing_engine_cpu_idle': 60
            }
        }
    }
}

mock_next_batch_result = {
    'current': {
        'batch_name': 'batch_4',
        'batch_index': 3,
        'batch_devices': {
            'dfb0cd32-46ca-4996-b155-806878d4e514': {
                'device_family': 'qfx',
                'device_fqname': ['device4'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_version': 'mx_version_1',
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.4',
                'device_password': 'contrail123',
                'device_username': 'root'
            }
        }
    },
    'status': 'success',
    'next': {
        'batch_name': None,
        'batch_index': None,
        'batch_devices': {}
    }
}

mock_all_devices_result = {
    'current': {
        'batch_name': None,
        'batch_index': None,
        'batch_devices': {}
    },
    'status': 'success',
    'next': {
        'batch_name': 'all',
        'batch_index': 0,
        'batch_devices': {
            'dfb0cd32-46ca-4996-b155-806878d4e515': {
                'device_family': 'qfx',
                'device_fqname': ['device5'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_version': 'mx_version_1',
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.5',
                'device_password': 'contrail123',
                'device_username': 'root'
            },
            'dfb0cd32-46ca-4996-b155-806878d4e514': {
                'device_family': 'qfx',
                'device_fqname': ['device4'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_version': 'mx_version_1',
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.4',
                'device_password': 'contrail123',
                'device_username': 'root'
            },
            'dfb0cd32-46ca-4996-b155-806878d4e517': {
                'device_family': 'qfx',
                'device_fqname': ['device7'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_version': 'mx_version_1',
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.7',
                'device_password': 'contrail123',
                'device_username': 'root'
            },
            'dfb0cd32-46ca-4996-b155-806878d4e516': {
                'device_family': 'qfx',
                'device_fqname': ['device6'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_version': 'mx_version_1',
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.6',
                'device_password': 'contrail123',
                'device_username': 'root'
            },
            'dfb0cd32-46ca-4996-b155-806878d4e511': {
                'device_family': 'qfx',
                'device_fqname': ['device1'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_version': 'mx_version_1',
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.1',
                'device_password': 'contrail123',
                'device_username': 'root'
            },
            'dfb0cd32-46ca-4996-b155-806878d4e513': {
                'device_family': 'qfx',
                'device_fqname': ['device3'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_version': 'mx_version_1',
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.3',
                'device_password': 'contrail123',
                'device_username': 'root'
            },
            'dfb0cd32-46ca-4996-b155-806878d4e512': {
                'device_family': 'qfx',
                'device_fqname': ['device2'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_version': 'mx_version_1',
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.2',
                'device_password': 'contrail123',
                'device_username': 'root'
            },
            'dfb0cd32-46ca-4996-b155-806878d4e518': {
                'device_family': 'qfx',
                'device_fqname': ['device8'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_image_version': 'mx_version_1',
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.8',
                'device_password': 'contrail123',
                'device_username': 'root'
            }
        }
    }
}

mock_device_info_result = {
    'status': 'success',
    'vpg_table': {
        'dfb0cd32-46ca-4996-b155-806878d4e521': {
            'name': 'vpg_1',
            'device_table': {
                'dfb0cd32-46ca-4996-b155-806878d4e513': [{
                    'fq_name': ['default-global-system-config', 'device_3',
                                'pi_2'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e532'
                }],
                'dfb0cd32-46ca-4996-b155-806878d4e512': [{
                    'fq_name': ['default-global-system-config', 'device_2',
                                'pi_1'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e531'
                }]
            }
        },
        'dfb0cd32-46ca-4996-b155-806878d4e522': {
            'name': 'vpg_2',
            'device_table': {
                'dfb0cd32-46ca-4996-b155-806878d4e515': [{
                    'fq_name': ['default-global-system-config', 'device_5',
                                'pi_3'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e533'
                }],
                'dfb0cd32-46ca-4996-b155-806878d4e516': [{
                    'fq_name': ['default-global-system-config', 'device_6',
                                'pi_4'],
                    'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e534'
                }]
            }
        }
    },
    'advanced_parameters': {
        'health_check_abort': True,
        'bulk_device_upgrade_count': 5,
        'Juniper': {
            'storm_control_flag_check': True,
            'l2_total_mac_count_check': True,
            'active_route_count_check': True,
            'fpc': {
                'fpc_cpu_5min_avg_check': True,
                'fpc_cpu_5min_avg': 50,
                'fpc_memory_heap_util': 45,
                'fpc_memory_heap_util_check': True
            },
            'alarm': {
                'system_alarm_check': True,
                'chassis_alarm_check': True
            },
            'bgp': {
                'bgp_down_peer_count_check': True,
                'bgp_flap_count_check': True,
                'bgp_flap_count': 4,
                'bgp_down_peer_count': 0,
                'bgp_peer_state_check': True
            },
            'interface': {
                'interface_drop_count_check': True,
                'interface_error_check': True,
                'interface_carrier_transition_count_check': True
            },
            'lacp': {
                'lacp_down_local_check': True,
                'lacp_down_peer_check': True
            },
            'routing_engine': {
                'routing_engine_cpu_idle_check': True,
                'routing_engine_cpu_idle': 60
            }
        }
    },
    'device_table': {
        'dfb0cd32-46ca-4996-b155-806878d4e511': {
            'vpg_info': {
                'buddies': [],
                'vpg_list': []
            },
            'target_multihomed_interface': [],
            'role': 'spine',
            'name': 'device_1',
            'basic': {
                'device_family': 'qfx',
                'device_fqname': ['default-global-system-config', 'device_1'],
                'device_serial_number': '12345',
                'device_product': 'qfx-10000',
                'device_hitless_upgrade': True,
                'device_vendor': 'juniper',
                'device_management_ip': '1.1.1.1',
                'device_username': 'root',
                'device_password': '***'
            },
            'err_msgs': [],
            'physical_role': 'spine',
            'rb_roles': ['DC-Gateway'],
            'uuid': 'dfb0cd32-46ca-4996-b155-806878d4e511'
        }
    }
}

mock_validate_result = {
    'error_msg': "Fabric is hitless",
    'status': "success"
}

mock_validate_result_failure = {
    'error_msg': "Fabric will not be hitless because these roles will no longer be deployed",
    'status': "failure"
}


class TestHitlessUpgradeFilters(test_case.JobTestCase):
    fake_zk_client = FakeKazooClient()
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestHitlessUpgradeFilters, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestHitlessUpgradeFilters, cls).tearDownClass(*args, **kwargs)

    # end tearDownClass

    def setUp(self, extra_config_knobs=None):
        super(TestHitlessUpgradeFilters, self).setUp(extra_config_knobs=extra_config_knobs)
        self.init_test()
        return

    def init_test(self):
        self.mockFabric()
        self.mockPhysicalRoles()
        for id, val in list(mock_device_image_db.items()):
            self.mockDeviceImage(id)
        for id, val in list(mock_physical_router_db.items()):
            self.mockPhysicalRouter(id)
        for id, val in list(mock_physical_interface_db.items()):
            self.mockPhysicalInterface(id)
        for id, val in list(mock_virtual_port_group_db.items()):
            self.mockVirtualPortGroup(id)
        flexmock(job_utils.random).should_receive('shuffle').and_return()
        flexmock(VncApi).should_receive('__init__')
        flexmock(VncApi).should_receive('__new__').and_return(self._vnc_lib)
        flexmock(self._vnc_lib).should_receive('job_template_read').\
            and_return(self.mockJobTemplate("hitless_upgrade_strategy_template"))

    def test_get_hitless_upgrade_plan(self):
        hitless_filter = FilterModule()
        upgrade_plan = hitless_filter.get_hitless_upgrade_plan(mock_job_ctx,
                        mock_image_upgrade_list)
        for device_uuid, device_info in list(upgrade_plan['device_table'].items()):
            device_info['basic']['device_password'] = '***'
            for buddy in device_info['vpg_info']['buddies']:
                buddy['password'] = '***'
        upgrade_plan['device_table'] = json.loads(json.dumps(upgrade_plan['device_table']))
        mock_upgrade_plan_result['device_table'] = json.loads(json.dumps(mock_upgrade_plan_result['device_table']))
        upgrade_plan['report'] = json.loads(json.dumps(upgrade_plan['report'])).replace("[u'", "['")
        upgrade_plan['results'] = json.loads(json.dumps(upgrade_plan['results'])).replace("[u'", "['")
        self.assertEqual(mock_upgrade_plan_result, upgrade_plan)

    def test_get_next_batch(self):
        hitless_filter = FilterModule()
        next_batch = hitless_filter.get_next_batch(mock_job_ctx,
                                                   mock_upgrade_plan,
                                                   DEV_UUID4)
        self.assertEqual(mock_next_batch_result, next_batch)

    def test_get_all_devices(self):
        hitless_filter = FilterModule()
        all_devices = hitless_filter.get_all_devices(mock_job_ctx,
                                                     mock_upgrade_plan)
        self.assertEqual(mock_all_devices_result, all_devices)

    def test_get_device_info(self):
        hitless_filter = FilterModule()
        device_info = hitless_filter.get_device_info(mock_job_ctx,
                                                     DEV_UUID1)
        for device_uuid, dev_info in list(device_info['device_table'].items()):
            dev_info['basic']['device_password'] = '***'
            for buddy in dev_info['vpg_info']['buddies']:
                buddy['password'] = '***'
        self.assertEqual(mock_device_info_result, device_info)

    def test_validate_critical_roles(self):
        hitless_filter = FilterModule()
        result = hitless_filter.validate_critical_roles(mock_job_ctx,
                                                        [DEV_UUID1])
        self.assertEqual(mock_validate_result, result)
        result = hitless_filter.validate_critical_roles(mock_job_ctx,
                                                        [DEV_UUID1,DEV_UUID4])
        self.assertIn(mock_validate_result_failure['error_msg'],
                      result['error_msg'])
        self.assertIn('DC-Gateway', result['error_msg'])
        self.assertIn('spine', result['error_msg'])

    def mockFabric(self):
        try:
            fabric_obj = Fabric(name='fab01')
            fabric_obj.uuid = FAB_UUID1
            fabric_obj.fq_name = [DGSC, 'fab01']
            cred = UserCredentials(username='root',
                                    password='c0ntrail123')
            credentials = DeviceCredential(credential=cred)
            fabric_credentials = DeviceCredentialList(device_credential=[credentials])
            fabric_obj.set_fabric_credentials(fabric_credentials)
            fabric_obj.set_annotations(KeyValuePairs([
                KeyValuePair(key='hitless_upgrade_input', value=mock_job_template_input_schema)]))

            self._vnc_lib.fabric_create(fabric_obj)
        except RefsExistError:
            logger.info("Fabric {} already exists".format('fab01'))
        except Exception as ex:
            logger.error("ERROR creating fabric {}: {}".format('fab01', ex))
        finally:
            self.fabric_obj = self._vnc_lib.fabric_read(id=FAB_UUID1)

    def mockPhysicalRoles(self):
        try:
            phy_role_leaf = PhysicalRole(name='leaf')
            self._vnc_lib.physical_role_create(phy_role_leaf)
            phy_role_spine = PhysicalRole(name='spine')
            self._vnc_lib.physical_role_create(phy_role_spine)
        except RefsExistError:
            logger.info("Physical role already exists")
        except Exception as ex:
            logger.error("ERROR creating physical role: {}".format(ex))

    def mockJobTemplate(self, fqname):
        try:
            templates = mock_job_templates_list['job-templates']
            for jt in templates:
                if fqname == jt['job-template']['fq_name'][-1]:
                    job_template_obj = JobTemplate().from_dict(**jt['job-template'])
                    return job_template_obj
        except RefsExistError:
            logger.info("Job template {} already exists".format(fqname))
        except Exception as ex:
            logger.error("ERROR creating job template {}: {}".format(fqname, ex))
            return None

    def mockDeviceImage(self, id):
        try:
            image = mock_device_image_db[id]
            image_obj = DeviceImage(
                name = image['name'],
                device_image_os_version = image['device_image_os_version'],
                device_image_device_family = image['device_image_device_family']
            )
            image_obj.uuid = id
            self._vnc_lib.device_image_create(image_obj)
        except RefsExistError:
            logger.info("Device image {} already exists".format(id))
        except Exception as ex:
            logger.error("ERROR creating device image {}: {}".format(id, ex))

    def mockPhysicalRouter(self, id):
        try:
            device = mock_physical_router_db[id]
            device_obj = PhysicalRouter(
                name = device['fq_name'][-1],
                display_name = device["display_name"],
                physical_router_role = device["physical_router_role"],
                routing_bridging_roles = RoutingBridgingRolesType(rb_roles=device['routing_bridging_roles']),
                physical_router_user_credentials = UserCredentials(username='root',
                                       password='c0ntrail123'),
                fq_name = device["fq_name"],
                physical_router_vendor_name = device["physical_router_vendor_name"],
                physical_router_device_family = device["physical_router_device_family"],
                physical_router_product_name = device["physical_router_product_name"],
                physical_router_serial_number = device["physical_router_serial_number"],
                physical_router_management_ip = device["physical_router_management_ip"],
                physical_router_os_version = device["physical_router_os_version"]
            )
            device_obj.uuid = id
            device_obj.add_fabric(self.fabric_obj)
            phy_role = self._vnc_lib.physical_role_read(fq_name=['default-global-system-config',device["physical_router_role"]])
            device_obj.set_physical_role(phy_role)
            self._vnc_lib.physical_router_create(device_obj)
        except RefsExistError:
            logger.info("Physical router {} already exists".format(id))
        except Exception as ex:
            logger.error("ERROR creating physical router {}: {}".format(id, ex))

    def mockPhysicalInterface(self, id):
        try:
            pi = mock_physical_interface_db[id]
            pi_obj = PhysicalInterface(
                name=pi['fq_name'][-1])
            pi_obj.uuid = id
            pi_obj.parent_uuid = pi['parent_uuid']
            pi_obj.fq_name = pi['fq_name']
            self._vnc_lib.physical_interface_create(pi_obj)
        except RefsExistError:
            logger.info("Physical interface {} already exists".format(id))
        except Exception as ex:
            logger.error("ERROR creating physical interface {}: {}".format(id, ex))

    def mockVirtualPortGroup(self, id):
        try:
            attr_obj = VpgInterfaceParametersType(ae_num=1)
            vpg = mock_virtual_port_group_db[id]
            vpg_obj = VirtualPortGroup(
                name=vpg['name'],
                fq_name=vpg['fq_name'],
                parent_type='fabric',
                parent_uuid=FAB_UUID1
            )
            vpg_obj.uuid = id
            self._vnc_lib.virtual_port_group_create(vpg_obj)

            for ref in vpg['refs']:
                self._vnc_lib.ref_update(
                    'virtual-port-group',
                    id,
                    'physical-interface',
                    ref['uuid'],
                    {'attr': attr_obj.__dict__},
                    'ADD')
        except RefsExistError:
            logger.info("VPG {} already exists".format(id))
        except Exception as ex:
            logger.error("ERROR creating VPG {}: {}".format(id, ex))
