#!/usr/bin/python
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
import gevent
import gevent.monkey
gevent.monkey.patch_all(thread=False)
import sys
import os
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

from cli_sync import FilterModule

sys.path.append('../fabric-ansible/ansible-playbooks/module_utils')

from job_manager import job_utils
from vnc_api.vnc_api import VncApi

from vnc_api.vnc_api import (
    Fabric,
    PhysicalRouter,
    PhysicalInterface,
    CliConfig,
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
DEV_UUID1 = "dfb0cd32-46ca-4996-b155-806878d4e511"
CLI_CONFIG_UUID = "dfb0cd32-46ca-4996-b155-806878d4e517"

mock_job_input = {
    "fabric_uuid": FAB_UUID1,
    "devices_cli": [{"device_uuid": DEV_UUID1,
                     "cli_objects": [
                         {"time": "2020-01-19 25:35:48", "status": "accept"}]}]
}

mock_job_ctx = {
    "fabric_fqname": "fab01",
    "job_template_fqname": [DGSC, "cli_sync_template"],
    "job_input": mock_job_input,
    "vnc_api_init_params": {"admin_password": "c0ntrail123"}
}

mock_device_cli_list_approve = [{"device_uuid": DEV_UUID1,
                         "cli_objects": [
                             {"time": "2020-01-19 25:35:48",
                              "status": "accept"}]}]

mock_device_cli_list_reject = [{"device_uuid": DEV_UUID1,
                         "cli_objects": [
                             {"time": "2020-01-19 25:35:48",
                              "status": "reject"}]}]

mock_cli_config_db = {
    CLI_CONFIG_UUID :{
        "fq_name": [DGSC, "5a12-qfx12", "5a12-qfx12_cli_config"],
        "parent_uuid": DEV_UUID1,
        "uuid": CLI_CONFIG_UUID,
        "commit_diff_list": {
            "commit_diff_info": [
                {
                    "username": "root",
                    "config_changes": "[edit system login]\n+ user Valley { \n+ uid 2004; \n+ class super-user; \n+ }",
                    "time": "2020-01-19 25:35:48"
                }
            ]
        }

    }
}
mock_physical_router_db = {
    DEV_UUID1: {
        "display_name": "5a12-qfx12",
        "physical_router_role": "spine",
        "routing_bridging_roles": ["CRB-Gateway"],
        "fq_name": [DGSC, "5a12-qfx12"],
        "physical_router_vendor_name": "juniper",
        "physical_router_device_family": "qfx",
        "physical_router_product_name": "qfx-10000",
        "physical_router_serial_number": "12345",
        "physical_router_management_ip": "1.1.1.1",
        "physical_router_os_version": "17.1.1",
        "device_username": "root",
        "device_password": "contrail123",
    }
}

mock_cli_config_commit_db = [{"username": "root",
                            "config_changes": "[edit system login]\n+ user Valley {\n+ uid 2004;\n+ class super-user;\n+ }\n",
                            "time": "2020-01-19 25:35:48"
                            }]

mock_data_dict = {
    "pr_commit_diff_list": [
        {
            "config_changes": "[edit system login]\n+ user Valley { \n+ uid 2004; \n+ class super-user; \n+ }",
            "time": "2020-01-19 25:35:48",
            "username": "root"
        }
    ]
    ,
    "pr_commit_item": [
        {
            "config_changes": "[edit system login]\n+ user Valley { \n+ uid 2004; \n+ class super-user; \n+ }",
            "time": "2020-01-19 25:35:48",
            "username": "root"
        }
    ],
    "cli_fq_name": [
        "default-global-system-config",
        "5a12-qfx12",
        "5a12-qfx12_cli_config"
    ]
}

mock_job_template_input_schema = """{
    "title": "CLI sync input",
    "$schema": "http://json-schema.org/draft-06/schema#",
    "type": "object",
    "additionalProperties": false,
    "properties": {
      "fabric_uuid": {
        "type": "string",
        "description": "Fabric UUID"
      },
      "devices_cli": {
        "type": "array",
        "items": {
          "title": "Devices CLI Items",
          "type": "object",
          "description": "Dictionaries of device details",
          "additionalProperties": false,
          "required": [
            "device_uuid",
            "cli_objects"
          ],
          "properties": {
            "device_uuid": {
              "type": "string",
              "title": "UUIDs of the device"
            },
            "cli_objects": {
              "type": "array",
              "title": "cli objects for that device",
              "items": {
                "type": "object",
                "required": [
                  "time",
                  "status"
                ],
                "properties": {
                  "time": {
                    "type": "string",
                    "title": "Timestamp of the cli change"
                  },
                  "status": {
                    "type": "string",
                    "title": "Accept or Reject the commit"
                  }
                }
              }
            }
          }
        }
      }
    }
  }
"""

mock_job_templates_list = {
    "job-templates": [
        {
            "job-template": {
                "job_template_concurrency_level": "fabric",
                "parent_uuid": "3abb3d8c-ec26-4360-bd2a-6470ac933f38",
                "job_template_output_schema": "{\"$schema\": \"http://json-schema.org/draft-06/schema#\", \"required\": [\"status\"], \"type\": \"object\", \"properties\": {\"status\": {\"enum\": [\"Success\", \"Failure\", \"Timeout\"], \"type\": \"string\", \"description\": \"Result status of the job\"}, \"message\": {\"type\": \"string\", \"description\": \"Should capture a summarized error message in case of Failures.\"}, \"results\": {\"type\": \"object\", \"description\": \"JSON object holding the job specific output details\"}}, \"title\": \"Generic Job Output\"}",
                "job_template_playbooks": {
                    "playbook_info": [
                        {
                            "vendor": "Juniper",
                            "job_completion_weightage": 100,
                            "sequence_no": "",
                            "device_family": "",
                            "multi_device_playbook": True,
                            "playbook_uri": "./opt/contrail/fabric_ansible_playbooks/cli_sync.yml"
                        }
                    ]
                },
                "job_template_output_ui_schema": "null",
                "parent_href": "http://172.19.65.253:8082/global-system-config/3abb3d8c-ec26-4360-bd2a-6470ac933f38",
                "parent_type": "global-system-config",
                "href": "http://172.19.65.253:8082/job-template/2f5594bc-fa5e-4465-8877-a9e37291b4a4",
                "id_perms": {
                    "enable": True,
                    "uuid": {
                        "uuid_mslong": 3410795832178263141,
                        "uuid_lslong": 9833515105731589284
                    },
                    "created": "2019-12-17T20:25:40.952314",
                    "description": "",
                    "creator": "",
                    "user_visible": True,
                    "last_modified": "2020-01-09T10:31:10.409558",
                    "permissions": {
                        "owner": "cloud-admin",
                        "owner_access": 7,
                        "other_access": 7,
                        "group": "cloud-admin-group",
                        "group_access": 7
                    }
                },
                "display_name": "Sync cli config with Contrail",
                "name": "cli_sync_template",
                "fq_name": [
                    "default-global-system-config",
                    "cli_sync_template"
                ],
                "uuid": "2f5594bc-fa5e-4465-8877-a9e37291b4a4",
                "job_template_input_ui_schema": "null",
                "perms2": {
                    "owner": "cloud-admin",
                    "owner_access": 7,
                    "global_access": 0,
                    "share": []
                },
                "job_template_type": "workflow",
                "job_template_input_schema": "{\"additionalProperties\": false, \"$schema\": \"http://json-schema.org/draft-06/schema#\", \"type\": \"object\", \"properties\": {\"devices_cli\": {\"items\": {\"description\": \"Dictionaries of device details\", \"title\": \"Devices CLI Items\", \"required\": [\"device_uuid\", \"cli_objects\"], \"additionalProperties\": false, \"type\": \"object\", \"properties\": {\"cli_objects\": {\"items\": {\"required\": [\"time\", \"status\"], \"type\": \"object\", \"properties\": {\"status\": {\"type\": \"string\", \"title\": \"Accept or Reject the commit\"}, \"time\": {\"type\": \"string\", \"title\": \"Timestamp of the cli change\"}}}, \"type\": \"array\", \"title\": \"cli objects for that device\"}, \"device_uuid\": {\"type\": \"string\", \"title\": \"UUIDs of the device\"}}}, \"type\": \"array\"}, \"fabric_uuid\": {\"type\": \"string\", \"description\": \"Fabric UUID\"}}, \"title\": \"CLI sync input\"}"
            }
        }
    ]
}

class TestCliSyncFilters(test_case.JobTestCase):
    fake_zk_client = FakeKazooClient()
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestCliSyncFilters, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestCliSyncFilters, cls).tearDownClass(*args, **kwargs)

    # end tearDownClass

    def setUp(self, extra_config_knobs=None):
        super(TestCliSyncFilters, self).setUp(extra_config_knobs=extra_config_knobs)
        self.init_test()
        return

    def tearDown(self):
        self._newMock.reset()
        self._initMock.reset()
        self._readMock.reset()
        super(TestCliSyncFilters, self).tearDown()

    def init_test(self):
        self.mockFabric()
        for id, val in list(mock_physical_router_db.items()):
            self.mockPhysicalRouter(id)
        self.mockCreateFile()
        flexmock(job_utils.random).should_receive('shuffle').and_return()
        self._initMock = flexmock(VncApi).should_receive('__init__')
        self._newMock = flexmock(VncApi).should_receive('__new__'). \
            and_return(self._vnc_lib)
        self._readMock = flexmock(self._vnc_lib). \
            should_receive('job_template_read'). \
            and_return(
            self.mockJobTemplate("cli_sync_template"))

    def test_create_cli_obj(self):
        cli_filter = FilterModule()
        self.mockCreateFile()
        device = mock_physical_router_db[DEV_UUID1]
        device_name = device["display_name"]
        device_mgmt_ip = device["physical_router_management_ip"]
        updated_cli_obj = cli_filter.create_cli_obj(mock_job_ctx,
                                                    device_mgmt_ip, device_name)
        obj_to_compare = self._vnc_lib.obj_to_dict(updated_cli_obj)
        commit_diff_list = obj_to_compare.get('commit_diff_list')
        obj_to_compare = commit_diff_list['commit_diff_info']
        self.assertEqual(mock_cli_config_commit_db, obj_to_compare)


    def test_cli_sync_approve(self):
        cli_filter = FilterModule()
        for id, val in list(mock_cli_config_db.items()):
            self.mockCliConfig(id)
        device = mock_physical_router_db[DEV_UUID1]
        device_name = device["display_name"]
        device_uuid = DEV_UUID1
        device_mgmt_ip = device["physical_router_management_ip"]
        data_dict = cli_filter.cli_sync(mock_job_ctx, device_name, device_uuid,
                                        mock_device_cli_list_approve, device_mgmt_ip)
        self.assertEqual(mock_data_dict, data_dict)

    def test_cli_sync_reject(self):
        cli_filter = FilterModule()
        for id, val in list(mock_cli_config_db.items()):
            self.mockCliConfig(id)
        device = mock_physical_router_db[DEV_UUID1]
        device_name = device["display_name"]
        device_uuid = DEV_UUID1
        device_mgmt_ip = device["physical_router_management_ip"]
        data_dict = cli_filter.cli_sync(mock_job_ctx, device_name, device_uuid,
                                        mock_device_cli_list_reject, device_mgmt_ip)
        self.assertEqual(mock_data_dict, data_dict)

    def mockFabric(self):
        try:
            fabric_obj = Fabric(name='fab01')
            fabric_obj.uuid = FAB_UUID1
            fabric_obj.fq_name = [DGSC, 'fab01']
            cred = UserCredentials(username='root',
                                   password='c0ntrail123')
            credentials = DeviceCredential(credential=cred)
            fabric_credentials = DeviceCredentialList(
                device_credential=[credentials])
            fabric_obj.set_fabric_credentials(fabric_credentials)
            fabric_obj.set_annotations(KeyValuePairs([
                KeyValuePair(key='cli_sync_input',
                             value=mock_job_template_input_schema)]))
            self._vnc_lib.fabric_create(fabric_obj)
        except RefsExistError:
            logger.info("Fabric {} already exists".format('fab01'))
        except Exception as ex:
            logger.error("ERROR creating fabric {}: {}".format('fab01', ex))

    def mockJobTemplate(self, fqname):
        try:
            templates = mock_job_templates_list['job-templates']
            for jt in templates:
                if fqname == jt['job-template']['fq_name'][-1]:
                    job_template_obj = JobTemplate().from_dict(
                        **jt['job-template'])
                    return job_template_obj
        except RefsExistError:
            logger.info("Job template {} already exists".format(fqname))
        except Exception as ex:
            logger.error(
                "ERROR creating job template {}: {}".format(fqname, ex))
            return None

    def mockPhysicalRouter(self, id):
        try:
            device = mock_physical_router_db[id]
            device_obj = PhysicalRouter(
                name=device['fq_name'][-1],
                display_name=device["display_name"],
                physical_router_role=device["physical_router_role"],
                routing_bridging_roles=RoutingBridgingRolesType(
                    rb_role=device['routing_bridging_roles']),
                physical_router_user_credentials=UserCredentials(
                    username='root',
                    password='c0ntrail123'),
                fq_name=device["fq_name"],
                physical_router_vendor_name=device[
                    "physical_router_vendor_name"],
                physical_router_device_family=device[
                    "physical_router_device_family"],
                physical_router_product_name=device[
                    "physical_router_product_name"],
                physical_router_serial_number=device[
                    "physical_router_serial_number"],
                physical_router_management_ip=device[
                    "physical_router_management_ip"],
                physical_router_os_version=device["physical_router_os_version"]
            )
            device_obj.uuid = id
            self._vnc_lib.physical_router_create(device_obj)
        except RefsExistError:
            logger.info("Physical router {} already exists".format(id))
        except Exception as ex:
            logger.error("ERROR creating physical router {}: {}".format(id, ex))

    def mockCliConfig(self, id):
        try:
            cli_config = mock_cli_config_db[id]
            cli_obj = CliConfig(
                name=cli_config['fq_name'][-1],
                commit_diff_list=cli_config["commit_diff_list"]
            )
            cli_obj.uuid = id
            cli_obj.parent_uuid = cli_config['parent_uuid']
            cli_obj.fq_name = cli_config['fq_name']
            self._vnc_lib.cli_config_create(cli_obj)
        except RefsExistError:
            logger.info("Cli config {} already exists".format(id))
        except Exception as ex:
            logger.error("ERROR creating cli config {}: {}".format(id, ex))


    def mockCreateFile(self):
        try:
            path = "../fabric-ansible/ansible-playbooks/manual_config/1.1.1.1"
            if not os.path.exists(path):
                os.makedirs(path)
            dirName = path + "/"
            f = open(dirName + "root_2020-01-19_25:35:48_UTC.diff", "w+")
            f.write("[edit system login]\n+ user Valley {\n+ uid 2004;\n+ class super-user;\n+ }\n")
            f.close()
        except Exception as ex:
            logger.error("ERROR creating file {}".format(ex))
