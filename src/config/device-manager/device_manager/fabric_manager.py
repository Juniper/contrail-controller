#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

import json
import os
import requests
import time

from cfgm_common.exceptions import ResourceExhaustionError
from cfgm_common.utils import CamelCase, detailed_traceback, str_to_class
from dm_utils import PushConfigState
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from vnc_api.exceptions import NoIdError
from vnc_api.gen import resource_client
from vnc_api.vnc_api import VncApi


class FabricManager(object):

    _instance = None

    def __init__(self, args, logger):
        FabricManager._instance = self
        self._fabric_ansible_dir = args.fabric_ansible_dir
        self._logger = logger

        self._init_vnc_api(args)
        if PushConfigState.is_push_mode_ansible():
            self._load_init_data()
    # end __init__

    @classmethod
    def get_instance(cls):
        return cls._instance
    # end get_instance

    @classmethod
    def destroy_instance(cls):
        inst = cls.get_instance()
        if not inst:
            return
        cls._instance = None
    # end destroy_instance

    @classmethod
    def initialize(cls, args, logger):
        if not cls._instance:
            FabricManager(args, logger)
    # end _initialize

    def _init_vnc_api(self, args):
        # Retry till API server is up
        connected = False
        self._connection_state_update(args, ConnectionStatus.INIT)
        api_server_list = args.api_server_ip.split(',')
        while not connected:
            try:
                self._vnc_api = VncApi(
                    args.admin_user, args.admin_password,
                    args.admin_tenant_name, api_server_list,
                    args.api_server_port,
                    api_server_use_ssl=args.api_server_use_ssl)
                connected = True
                self._connection_state_update(args, ConnectionStatus.UP)
            except requests.exceptions.ConnectionError as e:
                # Update connection info
                self._connection_state_update(args, ConnectionStatus.DOWN, str(e))
                time.sleep(3)
            except ResourceExhaustionError:  # haproxy throws 503
                time.sleep(3)
    # end _init_vnc_api

    def _connection_state_update(self, args, status, message=None):
        ConnectionState.update(
            conn_type=ConnType.APISERVER, name='ApiServer',
            status=status,
            message=message or 'ApiServer Connection State updated',
            server_addrs=['%s:%s' % (args.api_server_ip,
                                     args.api_server_port)])
    # end _connection_state_update

    # Load init data for job playbooks like JobTemplates, Tags, etc
    def _load_init_data(self):
        """
        This function loads init data from a data file specified by the
        argument '--fabric_ansible_dir' to the database. The data file
        must be in JSON format and follow the format below:
        {
          "data": [
            {
              "object_type": "<vnc object type name>",
              "objects": [
                {
                  <vnc object payload>
                },
                ...
              ]
            },
            ...
          ]
        }

        Here is an example:
        {
          "data": [
            {
              "object_type": "tag",
              "objects": [
                {
                  "fq_name": [
                    "fabric=management_ip"
                  ],
                  "name": "fabric=management_ip",
                  "tag_type_name": "fabric",
                  "tag_value": "management_ip"
                }
              ]
            }
          ]
        }
        """
        try:
            json_data = self._load_json_data()
            if json_data is None:
                self._logger.error('Unable to load init data')
                return

            for item in json_data.get("data"):
                object_type = item.get("object_type")

                # Get the class name from object type
                cls_name = CamelCase(object_type)
                # Get the class object
                cls_ob = str_to_class(cls_name, resource_client.__name__)

                # saving the objects to the database
                for obj in item.get("objects"):
                    instance_obj = cls_ob.from_dict(**obj)

                    # create/update the object
                    fq_name = instance_obj.get_fq_name()
                    try:
                        uuid = self._vnc_api.fq_name_to_id(object_type, fq_name)
                        if object_type == "tag":
                            continue
                        instance_obj.set_uuid(uuid)
                        self._vnc_api._object_update(object_type, instance_obj)
                    except NoIdError:
                        self._vnc_api._object_create(object_type, instance_obj)

            for item in json_data.get("refs"):
                from_type = item.get("from_type")
                from_fq_name = item.get("from_fq_name")
                from_uuid = self._vnc_api.fq_name_to_id(from_type, from_fq_name)

                to_type = item.get("to_type")
                to_fq_name = item.get("to_fq_name")
                to_uuid = self._vnc_api.fq_name_to_id(to_type, to_fq_name)

                self._vnc_api.ref_update(from_type, from_uuid, to_type,
                                         to_uuid, to_fq_name, 'ADD')
        except Exception as e:
            err_msg = 'error while loading init data: %s\n' % str(e)
            err_msg += detailed_traceback()
            self._logger.error(err_msg)
    # end _load_init_data

    # Load json data from fabric_ansible_playbooks/conf directory
    def _load_json_data(self):
        json_file = self._fabric_ansible_dir + '/conf/predef_payloads.json'
        if not os.path.exists(json_file):
            msg = 'predef payloads file does not exist: %s' % json_file
            self._logger.error(msg)
            return None

        # open the json file
        with open(json_file) as data_file:
            input_json = json.load(data_file)

        # Loop through the json
        for item in input_json.get("data"):
            if item.get("object_type") == "job-template":
                for object in item.get("objects"):
                    fq_name = object.get("fq_name")[-1]
                    schema_name = fq_name.replace('template', 'schema.json')
                    with open(os.path.join(self._fabric_ansible_dir +
                            '/schema/', schema_name), 'r+') as schema_file:
                        schema_json = json.load(schema_file)
                        object["job_template_input_schema"] = json.dumps(
                            schema_json.get("input_schema"))
                        object["job_template_output_schema"] = json.dumps(
                            schema_json.get("output_schema"))
                        object["job_template_input_ui_schema"] = json.dumps(
                            schema_json.get("input_ui_schema"))
                        object["job_template_output_ui_schema"] = json.dumps(
                            schema_json.get("output_ui_schema"))
        return input_json
    # end _load_json_data

# end FabricManager
