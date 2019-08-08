#!/usr/bin/python

from builtins import object
from builtins import str
import json
import sys
import traceback

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import _task_done, _task_error_log, _task_log, \
    FilterLog
import jsonschema


class FilterModule(object):

    def filters(self):
        return {
            'validate_schema': self.validate_schema,
        }

    def _instantiate_filter_log_instance(self, device_name):
        FilterLog.instance("ValidateSchemaFilter", device_name)

    def validate_schema(self, schema_type, file_path,
                        json_obj, device_name):

        self._instantiate_filter_log_instance(device_name)
        try:
            _task_log("Starting to validate " + schema_type)
            with open(file_path) as f:
                schema_json = json.load(f)
            jsonschema.validate(json_obj, schema_json.get(schema_type))
            _task_done("Completed " + schema_type + " validation")
            return {'status': 'success'}
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            return {
                'status': 'failure',
                'error_msg': str(ex)
            }
