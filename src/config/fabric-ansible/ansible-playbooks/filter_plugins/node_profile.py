#!/usr/bin/python

from builtins import str
from builtins import map
from past.builtins import basestring
from builtins import object
import logging
import traceback
import json
import yaml
import sys
import base64
import uuid
import collections
from pprint import pformat

sys.path.append('/opt/contrail/fabric_ansible_playbooks/filter_plugins')
sys.path.append('/opt/contrail/fabric_ansible_playbooks/common')
from contrail_command import CreateCCNode, CreateCCNodeProfile
import jsonschema
from job_manager.job_utils import JobVncApi

class NodeProfileLog(object):
    _instance = None

    @staticmethod
    def instance():
        if not NodeProfileLog._instance:
            NodeProfileLog._instance = NodeProfileLog()
        return NodeProfileLog._instance
    # end instance

    @staticmethod
    def _init_logging():
        """
        :return: type=<logging.Logger>
        """
        logger = logging.getLogger('ServerFilter')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.WARN)

        formatter = logging.Formatter(
            '%(asctime)s %(levelname)-8s %(message)s',
            datefmt='%Y/%m/%d %H:%M:%S'
        )
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)
        return logger

    # end _init_logging

    def __init__(self):
        self._msg = None
        self._logs = []
        self._logger = NodeProfileLog._init_logging()

    # end __init__

    def logger(self):
        return self._logger

    # end logger

    def msg_append(self, msg):
        if msg:
            if not self._msg:
                self._msg = msg + ' ... '
            else:
                self._msg += msg + ' ... '

    # end log

    def msg_end(self):
        if self._msg:
            self._msg += 'done'
            self._logs.append(self._msg)
            self._logger.warn(self._msg)
            self._msg = None

    # end msg_end

    def dump(self):
        retval = ""
        for msg in self._logs:
            retval += msg + '\n'
        return retval
        # end dump
# end NodeProfileLog


class FilterModule(object):
    @staticmethod
    def _init_logging():
        logger = logging.getLogger('NodeProfileFilter1')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.WARN)

        formatter = logging.Formatter('%(asctime)s %(levelname)-8s %(message)s',
                                      datefmt='%Y/%m/%d %H:%M:%S')
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)

        return logger

    def __init__(self):
        self._logger = FilterModule._init_logging()

    # end __init__

    @staticmethod
    def _validate_job_ctx(job_ctx):
        vnc_api = JobVncApi.vnc_init(job_ctx)
        job_template_fqname = job_ctx.get('job_template_fqname')
        if not job_template_fqname:
            raise ValueError('Invalid job_ctx: missing job_template_fqname')

        job_input = job_ctx.get('input')
        if not job_input:
            raise ValueError('Invalid job_ctx: missing job_input')

        # retrieve job input schema from job template to validate the job input
        node_profile_template = vnc_api.job_template_read(
            fq_name=job_template_fqname
        )
        input_schema = node_profile_template.get_job_template_input_schema()
        input_schema = json.loads(input_schema)
        jsonschema.validate(job_input, input_schema)
        return job_input

    def filters(self):
        return {
            'add_node_profiles_from_file': self.add_node_profiles_from_file
        }

    def get_cc_node_profile_payload(self, node_profile_dict):
        cc_node_profile = {"resources":
            [{
                "kind": "node_profile",
                "data": {
                    "parent_type": "global-system-config",
                    "fq_name": ["default-global-system-config",
                                node_profile_dict['name']]
                }
            }]
        }
        cc_node_profile["resources"][0]["data"].update(node_profile_dict)
        fq_name = ["default-global-system-config", node_profile_dict['name']]
        return cc_node_profile, fq_name

    def create_cc_node_profile(self, node_profile_dict, np_object):

        np_uuid = None
        node_profiles = np_object.get_cc_node_profiles()
        fq_name = ['default-global-system-config', node_profile_dict['name']]
        for np in node_profiles['node-profiles']:
            if fq_name == np['node-profile']['fq_name']:
               np_uuid = np['node-profile']['uuid']
               break

        node_profile_dict['uuid'] = np_uuid
        cc_node_profile_payload, fq_name = self.get_cc_node_profile_payload(
            node_profile_dict)

        return cc_node_profile_payload, node_profile_dict['name']

    def convert(self, data):
        if isinstance(data, basestring):
            return str(data)
        elif isinstance(data, collections.Mapping):
            return dict(list(map(self.convert, iter(list(data.items())))))
        elif isinstance(data, collections.Iterable):
            return type(data)(list(map(self.convert, data)))
        else:
            return data

    def import_node_profiles(self, data, node_profile_object):
        added_node_profiles = []
        self._logger.warn("NP OBJECT" + pformat(data))
        node_profiles = node_profile_object.get_cc_node_profiles()
        self._logger.warn("NP CC Data " + pformat(node_profiles))
        if isinstance(data, dict) and "node_profile" in data:
            node_profile_list = data['node_profile']
            for node_profile_dict in node_profile_list:
                node_profile_payload, node_profile_name = \
                    self.create_cc_node_profile(node_profile_dict, node_profile_object)
                added_node_profiles.append(node_profile_name)
                self._logger.warn("NP OBJECT CREATION " + str(node_profile_name))
                self._logger.warn("NP OBJECT Data " + pformat(node_profile_payload))
                node_profile_object.create_cc_node_profile(node_profile_payload)
        return added_node_profiles

    # ***************** add_node_profiles_from_file filter *********************************

    def add_node_profiles_from_file(self, job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "encoded_node_profiles": "....",
                    "contrail_command_host": "....",
                    "encoded_file": "...."
                }
            }
        :return: Dictionary
            if success, returns
            [
                <list: imported node profiles>
            ]
            if failure, returns
            {
                'status': 'failure',
                'error_msg': <string: error message>,
                'np_log': <string: np_log>
            }
            """
        try:
            job_input = FilterModule._validate_job_ctx(job_ctx)
            self._logger.warn("Job INPUT:\n" + str(job_input))

            encoded_file = job_input.get("encoded_file")
            file_format = job_input.get("file_format")
            decoded = base64.decodestring(encoded_file)

            cluster_id = job_ctx.get('contrail_cluster_id')
            cluster_token = job_ctx.get('auth_token')

            cc_host = job_input.get('contrail_command_host')
            cc_username = job_input.get('cc_username')
            cc_password = job_input.get('cc_password')
            cc_node_profile_obj = CreateCCNodeProfile(cc_host, 
                                                      cluster_id, 
                                                      cluster_token,
                                                      cc_username,
                                                      cc_password)

            self._logger.warn("Starting Node Profile Import")

            if file_format.lower() == "yaml":
                data = yaml.load(decoded)
            elif file_format.lower() == "json":
                data = self.convert(json.loads(decoded))
            else:
                raise ValueError('File format not recognized. Only yaml or '
                                 'json supported')
            added_profiles = self.import_node_profiles(
                data, cc_node_profile_obj)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'np_log': NodeProfileLog.instance().dump()
            }

        return {
            'status': 'success',
            'added_profiles': added_profiles,
            'np_log': NodeProfileLog.instance().dump()
        }
