#!/usr/bin/python

import logging
import traceback
import json
import yaml
import sys
import base64
import uuid
import collections

sys.path.append('/opt/contrail/fabric_ansible_playbooks/filter_plugins')
sys.path.append('/opt/contrail/fabric_ansible_playbooks/common')
from plugin_ironic import *
from contrail_command import *
import jsonschema
from job_manager.job_utils import JobVncApi

type_name_translation_dict = {
    ("ipmi_port", int): ("ipmi_port", str),
    ("switch_name", str): ("switch_info", str),
    ("port_name", str): ("port_id", str),
    ("mac_address", str): ("address", str),
    ("cpus", str): ("cpus", int),
    ("local_gb", str): ("local_gb", int),
    ("memory_mb", str): ("memory_mb", int)
}

class ImportLog(object):
    _instance = None

    @staticmethod
    def instance():
        if not ImportLog._instance:
            ImportLog._instance = ImportLog()
        return ImportLog._instance
    # end instance

    @staticmethod
    def _init_logging():
        """
        :return: type=<logging.Logger>
        """
        logger = logging.getLogger('ServerFilter')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.INFO)

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
        self._logger = ImportLog._init_logging()

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
# end ImportLog


class FilterModule(object):
    @staticmethod
    def _init_logging():
        logger = logging.getLogger('ImportServerFilter')
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
        import_server_template = vnc_api.job_template_read(
            fq_name=job_template_fqname
        )
        input_schema = import_server_template.get_job_template_input_schema()
        jsonschema.validate(job_input, input_schema)
        return job_input

    def filters(self):
        return {
            'import_nodes_from_file': self.import_nodes_from_file
        }

    @staticmethod
    def translate(dict_to_trans):
        for k in dict_to_trans:
            if (k, type(dict_to_trans[k])) in \
                    type_name_translation_dict:
                trans_name, trans_type = type_name_translation_dict[
                    (k, type(dict_to_trans[k]))
                ]
                dict_to_trans[str(trans_name)] = trans_type(
                    dict_to_trans.pop(k)
                )
        return dict_to_trans

    def get_cc_port_payload(self, port_dict, local_link_dict):
        cc_port = {"kind": "port",
            "data": {
                "parent_type": "node",
                "parent_uuid": port_dict['node_uuid'],
                "name": port_dict['name'],
                "uuid": port_dict['uuid'],
                "bms_port_info": {
                    "pxe_enabled": port_dict.get('pxe_enabled',False),
                    "address": port_dict['address'],
                    "node_uuid": port_dict['node_uuid'],
                    "local_link_connection": local_link_dict
                }
            }
        }
        return cc_port

    def get_cc_node_payload(self, node_dict):
        cc_node = {"resources":
            [{
                "kind": "node",
                "data": {
                    "uuid": node_dict['uuid'],
                    "type": node_dict.get('type', "baremetal"),
                    "name": node_dict['name'],
                    "display_name": node_dict['name'],
                    "hostname": node_dict['name'],
                    "parent_type": "global-system-config",
                    "fq_name": ["default-global-system-config",
                                node_dict['uuid']],
                    "bms_info": {
                        "name": node_dict['name'],
                        "network_interface": "neutron",
                        "type": node_dict.get('type', "baremetal"),
                        "properties": node_dict['properties'],
                        "driver": node_dict.get('driver', "pxe_ipmitool"),
                        "driver_info": node_dict["driver_info"]
                    }
                }
            }]
        }

        return cc_node

    def convert_port_format(self, port_list, node_dict):
        cc_port_list = []
        generated_hostname = ""

        for port_dict in port_list:
            mac = port_dict['mac_address']
            port_dict['node_uuid'] = node_dict['uuid']

            if not port_dict.get('uuid', None):
                port_dict['uuid'] = str(uuid.uuid4())
            if not port_dict.get('name', None):
                port_dict['name'] = "p-" + mac.replace(":", "")[6:]
            if port_dict.get('pxe_enabled',False) and \
                    not node_dict.get('name',None):
                generated_hostname = "auto-" + mac.replace(":", "")[6:]

            port_dict = self.translate(port_dict)

            local_link_dict = {k: v for k, v in port_dict.iteritems() if k in
                               ['port_id', 'switch_info', 'switch_id']}

            cc_port = self.get_cc_port_payload(port_dict, local_link_dict)
            cc_port_list.append(cc_port)
        return generated_hostname, cc_port_list

    def create_cc_node(self, node_dict):
        port_list = node_dict['ports']

        if not node_dict.get('uuid', None):
            node_dict['uuid'] = str(uuid.uuid4())

        hostname, port_list = self.convert_port_format(port_list, node_dict)

        if not node_dict.get('name', None):
            node_dict['name'] = hostname

        for sub_dict in ['properties', 'driver_info']:
            node_sub_dict = node_dict.get(sub_dict, {})
            if node_sub_dict:
                node_dict[sub_dict] = self.translate(node_sub_dict)

        cc_node_payload = self.get_cc_node_payload(node_dict)
        cc_node_payload['resources'].extend(port_list)

        return cc_node_payload, node_dict['name']

    def import_yaml_format_nodes(self, data, ironic_object):
        data = yaml.load(data)
        added_nodes = []
        if isinstance(data, dict) and "nodes" in data:
            node_list = data['nodes']
            for node_dict in node_list:
                node_payload, node_name = self.create_cc_node(node_dict)
                added_nodes.append(node_name)
                ironic_object.cc_node.create_cc_node(node_payload)
        return added_nodes

    def convert(self, data):
        if isinstance(data, basestring):
            return str(data)
        elif isinstance(data, collections.Mapping):
            return dict(map(self.convert, data.iteritems()))
        elif isinstance(data, collections.Iterable):
            return type(data)(map(self.convert, data))
        else:
            return data

    def import_json_format_nodes(self, data, ironic_object):
        data = self.convert(json.loads(data))
        added_nodes = []
        if isinstance(data, dict) and "nodes" in data:
            node_list = data['nodes']
            for node_dict in node_list:
                node_payload, node_name = self.create_cc_node(node_dict)
                added_nodes.append(node_payload)
                ironic_object.cc_node.create_cc_node(node_payload)
        return added_nodes

    # ***************** import_nodes_from_file filter *********************************

    def import_nodes_from_file(self, job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "encoded_nodes": "....",
                    "ironic": {
                        "auth_url": "",
                        "username": "",
                        "password": ""
                    },
                    "contrail_command": {
                        "auth_host": "",
                        "username": "",
                        "password": ""
                    }
                }
            }
        :return: Dictionary
            if success, returns
            [
                <list: imported nodes>
            ]
            if failure, returns
            {
                'status': 'failure',
                'error_msg': <string: error message>,
                'discovery_log': <string: discovery_log>
            }
            """
        try:
            job_input = FilterModule._validate_job_ctx(job_ctx)
            self._logger.info("Job INPUT:\n" + str(job_input))

            encoded_file = job_input.get("encoded_file")
            file_format = job_input.get("file_format")
            decoded = base64.decodestring(encoded_file)

            ironic_auth_args = job_input.get('ironic')
            cc_auth_args = job_input.get('contrail_command')

            self._logger.info("Starting Server Import")
            ironic_node_object = ImportIronicNodes(auth_args=ironic_auth_args,
                                                   cc_auth_args=cc_auth_args)

            if file_format == "yaml":
                added_nodes = self.import_yaml_format_nodes(
                    decoded, ironic_node_object)
            elif file_format == "json":
                added_nodes = self.import_json_format_nodes(
                    decoded, ironic_node_object)
            else:
                raise ValueError('File format not recognized. Only yaml or '
                                 'json supported')
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'discovery_log': ImportLog.instance().dump()
            }

        return {
            'status': 'success',
            'added_nodes': added_nodes,
            'discover_log': ImportLog.instance().dump()
        }
