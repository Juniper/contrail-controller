#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""Contains utility functions used by the job manager."""

from __future__ import absolute_import

import base64
from builtins import object
from builtins import str
import collections
from enum import Enum
import json
import random
import traceback

from Crypto.Cipher import AES
from inflection import camelize
from jsonschema import Draft4Validator, validators
import vnc_api
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.vnc_api import VncApi

from .job_exception import JobException
from .job_messages import MsgBundle


PLAYBOOK_EOL_PATTERN = "*EOL*\n"


class JobStatus(Enum):
    STARTING = "STARTING"
    IN_PROGRESS = "IN_PROGRESS"
    SUCCESS = "SUCCESS"
    FAILURE = "FAILURE"


class JobVncApi(object):
    @staticmethod
    def vnc_init(job_ctx):
        # randomize list for load balancing, pass list for HA
        api_server_ip_list = job_ctx.get('api_server_host')
        random.shuffle(api_server_ip_list)
        if job_ctx.get('vnc_api_init_params') is not None:
            params = job_ctx.get('vnc_api_init_params')
            vnc_api = VncApi(
                params.get('admin_user'), params.get('admin_password'),
                params.get('admin_tenant_name'), api_server_ip_list,
                params.get('api_server_port'),
                api_server_use_ssl=params.get('api_server_use_ssl'))
        elif job_ctx.get('auth_token') is not None:
            vnc_api = VncApi(
                api_server_host=api_server_ip_list,
                auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                auth_token=job_ctx.get('auth_token')
            )
        else:
            vnc_api = VncApi()
        return vnc_api

    @staticmethod
    def get_vnc_cls(object_type):
        cls_name = camelize(object_type)
        return getattr(vnc_api.gen.resource_client, cls_name, None)

    @staticmethod
    def decrypt_password(encrypted_password=None, admin_password=None):
        if admin_password is None:
            raise ValueError("Admin Password must be specified")

        if encrypted_password is None:
            raise ValueError("No password to decrypt")

        key = admin_password.rjust(16)
        cipher = AES.new(key, AES.MODE_ECB)

        password = cipher.decrypt(base64.b64decode(encrypted_password))
        return password.strip()


class JobFileWrite(object):
    JOB_PROGRESS = 'JOB_PROGRESS##'
    PLAYBOOK_OUTPUT = 'PLAYBOOK_OUTPUT##'
    JOB_LOG = 'JOB_LOG##'
    PROUTER_LOG = 'PROUTER_LOG##'
    GEN_DEV_OP_RES = 'GENERIC_DEVICE##'

    def __init__(self, logger, ):
        """Initializes JobFileWrite."""
        self._logger = logger

    def write_to_file(self, exec_id, pb_id, marker, msg):
        try:
            fname = '/tmp/%s' % str(exec_id)
            with open(fname, "a") as f:
                line_in_file = "%s%s%s%s%s" % (
                    str(pb_id), marker, msg, marker, PLAYBOOK_EOL_PATTERN)
                f.write(line_in_file)
        except Exception as ex:
            self._logger.error("Failed to write_to_file: %s\n%s\n" % (
                str(ex), traceback.format_exc()
            ))


class JobUtils(object):

    def __init__(self, job_execution_id, job_template_id, logger, vnc_api):
        """Initializes JobUtils."""
        self._job_execution_id = job_execution_id
        self._job_template_id = job_template_id
        self._logger = logger
        self._vnc_api = vnc_api

    def read_job_template(self):
        try:
            job_template = self._vnc_api.job_template_read(
                id=self._job_template_id)
            self._logger.debug("Read job template %s from "
                               "database" % self._job_template_id)
        except Exception:
            msg = MsgBundle.getMessage(MsgBundle.READ_JOB_TEMPLATE_ERROR,
                                       job_template_id=self._job_template_id)
            raise JobException(msg, self._job_execution_id)
        return job_template


class JobAnnotations(object):

    def __init__(self, vnc_api):
        """Initializes JobAnnotations."""
        self.vncapi = vnc_api

    skip_validators = ['required', 'minProperties', 'maxProperties']

    # Extension to populate default values when creating JSON data from schema
    def _extend_with_default(self, validator_class):
        validate_properties = validator_class.VALIDATORS["properties"]

        def set_defaults(validator, properties, instance, schema):
            for obj_property, subschema in properties.items():
                if "default" in subschema:
                    instance.setdefault(obj_property, subschema["default"])
            for error in validate_properties(
                    validator, properties, instance, schema):
                yield error
        extended_validators = validators.extend(
            validator_class, {"properties": set_defaults},
        )
        VALIDATORS = extended_validators.VALIDATORS
        for _validator in self.skip_validators:
            if VALIDATORS.get(_validator):
                del(VALIDATORS[_validator])
        return extended_validators
    # end _extend_with_default

    # Generate default json object given the schema
    def _generate_default_schema_json(self, input_schema):
        return_json = {}
        default_validator = self._extend_with_default(Draft4Validator)
        default_validator(input_schema).validate(return_json)
        return return_json
    # end _generate_default_schema_json

    # Generate default json given the job template
    def generate_default_json(self, job_template_fqname):
        job_template_obj = self.vncapi.job_template_read(
            fq_name=job_template_fqname)
        input_schema = job_template_obj.get_job_template_input_schema()
        input_schema = json.loads(input_schema)
        return self._generate_default_schema_json(input_schema)
    # end _generate_default_json

    # Recursive update of nested dict
    def dict_update(self, d, u):
        for k, v in u.items():
            if isinstance(v, collections.Mapping):
                d[k] = self.dict_update(d.get(k, {}), v)
            else:
                d[k] = v
        return d
    # end dict_update

    # Store the job input on the fabric object for UI to retrieve later
    def cache_job_input(self, fabric_uuid, job_template_name, job_input):
        fabric_obj = self.vncapi.fabric_read(id=fabric_uuid)
        fabric_obj.add_annotations(
            KeyValuePair(key=job_template_name,
                         value=json.dumps(job_input)))
        self.vncapi.fabric_update(fabric_obj)
    # end _cache_job_input
