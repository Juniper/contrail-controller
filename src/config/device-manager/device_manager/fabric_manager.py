#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

import json
import os

from cfgm_common.utils import CamelCase, detailed_traceback, str_to_class
from vnc_api.exceptions import NoIdError
from vnc_api.gen import resource_client


class FabricManager(object):

    _instance = None

    def __init__(self, args, logger, vnc_api):
        FabricManager._instance = self
        self._fabric_ansible_dir = args.fabric_ansible_dir
        self._logger = logger
        self._vnc_api = vnc_api
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
    def initialize(cls, args, logger, vnc_api):
        if not cls._instance:
            FabricManager(args, logger, vnc_api)
    # end _initialize

    # Load init data for job playbooks like JobTemplates, Tags, etc
    def _load_init_data(self):
        """
        Load init data for job playbooks.

        This function loads init data from a data file specified by the
        argument '--fabric_ansible_dir' to the database. The data file
        must be in JSON format and follow the format below

        "my payload": {
            "object_type": "tag"
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
                        uuid = self._vnc_api.fq_name_to_id(
                            object_type, fq_name)
                        if object_type == "tag":
                            continue
                        instance_obj.set_uuid(uuid)
                        # Update config json inside role-config object
                        if object_type == 'role-config':
                            role_config_obj = self._vnc_api.\
                                role_config_read(id=uuid)
                            cur_config_json = json.loads(
                                role_config_obj.get_role_config_config())
                            def_config_json = json.loads(
                                instance_obj.get_role_config_config())
                            def_config_json.update(cur_config_json)
                            instance_obj.set_role_config_config(
                                json.dumps(def_config_json)
                            )
                        self._vnc_api._object_update(object_type, instance_obj)
                    except NoIdError:
                        self._vnc_api._object_create(object_type, instance_obj)

            for item in json_data.get("refs"):
                from_type = item.get("from_type")
                from_fq_name = item.get("from_fq_name")
                from_uuid = self._vnc_api.fq_name_to_id(
                    from_type, from_fq_name)

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
                for obj in item.get("objects"):
                    fq_name = obj.get("fq_name")[-1]
                    schema_name = fq_name.replace('template', 'schema.json')
                    with open(os.path.join(self._fabric_ansible_dir +
                              '/schema/', schema_name), 'r+') as schema_file:
                        schema_json = json.load(schema_file)
                        obj["job_template_input_schema"] = json.dumps(
                            schema_json.get("input_schema"))
                        obj["job_template_output_schema"] = json.dumps(
                            schema_json.get("output_schema"))
                        obj["job_template_input_ui_schema"] = json.dumps(
                            schema_json.get("input_ui_schema"))
                        obj["job_template_output_ui_schema"] = json.dumps(
                            schema_json.get("output_ui_schema"))
        return input_json
    # end _load_json_data

# end FabricManager
