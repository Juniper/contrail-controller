#!/usr/bin/python

from builtins import map
#from builtins import str
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
        input_schema = json.loads(input_schema)
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
                "fq_name": port_dict['fq_name'],
                "bms_port_info": {
                    "pxe_enabled": port_dict.get('pxe_enabled',False),
                    "address": port_dict['address'],
                    "local_link_connection": local_link_dict
                }
            }
        }
        if port_dict.get('dvs_name'):
            cc_port['data']['esxi_port_info'] = {'dvs_name': port_dict.get('dvs_name') }

        return cc_port

    def get_cc_node_payload(self, node_dict, node_uuid):
        cc_node = {"resources":
            [{
                "kind": "node",
                "data": {
                    "node_type": node_dict.get('node_type', "baremetal"),
                    "name": node_dict['name'],
                    "uuid": node_dict['uuid'],
                    "display_name": node_dict['display_name'],
                    "hostname": node_dict['hostname'],
                    "parent_type": "global-system-config",
                    "fq_name": ["default-global-system-config",
                                node_dict['name']],
                    "bms_info": {
                        "name": node_dict['name'],
                        "network_interface": "neutron",
                        "type": node_dict.get('type', "baremetal"),
                        "properties": node_dict.get('properties', {}),
                        "driver": node_dict.get('driver', "pxe_ipmitool"),
                        "driver_info": node_dict.get("driver_info", {})
                    }
                }
            }]
        }

        return cc_node

    def convert_port_format(self, port_list, node_dict, node_ports):
        cc_port_list = []

        for port_dict in port_list:
            mac = port_dict['mac_address']
            name = port_dict.get('name', "")
            port_dict['node_uuid'] = node_dict['uuid']
            port_uuid = None
            for node_port in node_ports:
                node_port_name = node_port.get('name', None)
                if node_port_name and node_port_name == name:
                    port_uuid = node_port['uuid']
                    break
                node_mac = node_port['bms_port_info'].get('address')
                if node_mac == mac:
                    port_uuid = node_port['uuid']
                    break

            port_dict['uuid'] = port_uuid
            if not port_dict.get('name', None):
                port_dict['name'] = "p-" + mac.replace(":", "")[6:]
            port_dict['fq_name'] = ['default-global-system-config', 
                                    node_dict['name'],
                                    port_dict['name'] ]

            port_dict = self.translate(port_dict)

            local_link_dict = {k: v for k, v in list(port_dict.items()) if k in
                               ['port_id', 'switch_info', 'switch_id']}

            self._logger.warn("dvs-name:: " + str(port_dict.get('dvs_name')))
            cc_port = self.get_cc_port_payload(port_dict, local_link_dict)
            cc_port_list.append(cc_port)
        return cc_port_list

    def generate_node_name(self, port_list):
        generated_hostname = ""
        for port in port_list:
            mac = port['mac_address']
            if port.get('pxe_enabled',False):
                generated_hostname = "auto-" + mac.replace(":", "")[6:]
                break
        if generated_hostname == "":
            # no pxe-enabled=true field
            if len(port_list) > 0:
                generated_hostname = "auto-" + port_list[0]['mac_address'].replace(":", "")[6:]

        return generated_hostname
    
        
    def create_cc_node(self, node_dict, cc_node_obj):
        port_list = node_dict.get('ports',[])

        if not node_dict.get("name", None):
            if node_dict.get('hostname', None):
                node_dict['name'] = node_dict['hostname']
            elif len(port_list) > 0:
                node_name = self.generate_node_name(port_list)
                node_dict['name'] = node_name
            else:
                # we are out of luck, we need to exit
                return {}, "", []

        node_uuid = None
        node_ports = []
        node_fq_name = ['default-global-system-config', node_dict['name']]
        cc_nodes = cc_node_obj.get_cc_nodes()
        for node in cc_nodes['nodes']:
            if node_fq_name == node['node']['fq_name']:
                node_uuid = node['node']['uuid']
                node_ports = node['node'].get('ports', [])
                self._logger.warn("CC NODES:: " + pformat(node))
                break

        node_dict['uuid'] = node_uuid
        port_list = self.convert_port_format(port_list, node_dict, node_ports)

        if not node_dict.get('hostname', None):
            node_dict['hostname'] = node_dict['name']

        if not node_dict.get('display_name', None):
            node_dict['display_name'] = node_dict['name']

        for sub_dict in ['properties', 'driver_info']:
            node_sub_dict = node_dict.get(sub_dict, {})
            if node_sub_dict:
                node_dict[sub_dict] = self.translate(node_sub_dict)

        cc_node_payload = self.get_cc_node_payload(node_dict, node_uuid)

        return cc_node_payload, node_dict['name'], port_list

    def convert(self, data):
        if isinstance(data, basestring):
            return str(data)
        elif isinstance(data, collections.Mapping):
            return dict(list(map(self.convert, iter(list(data.items())))))
        elif isinstance(data, collections.Iterable):
            return type(data)(list(map(self.convert, data)))
        else:
            return data

    def import_nodes(self, data, cc_node_obj):
        added_nodes = []
        if isinstance(data, dict) and "nodes" in data:
            node_list = data['nodes']
            #self._logger.warn("Creting Job INPUT:" + pformat(node_list))

            for node_dict in node_list:
                node_payload, node_name, port_list = self.create_cc_node(node_dict, cc_node_obj)
                if node_name == "":
                    self._logger.warn("IGNORING Creation of : " + pformat(node_dict))
                    continue
                added_nodes.append(node_payload)
                self._logger.warn("Creating : " + str(node_name))
                self._logger.warn("Creating : " + pformat(node_payload))
                resp = cc_node_obj.create_cc_node(node_payload)
                node_uuid = json.loads(resp)[0]['data']['uuid']
                for port in port_list:
                    if not port['data']['parent_uuid']:
                        port['data']['parent_uuid'] = node_uuid
                port_payload = {"resources" : []}
                port_payload['resources'].extend(port_list)
                self._logger.warn("port-create: " + pformat(port_payload))
                cc_node_obj.create_cc_node(port_payload)
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
                    "contrail_command_host": "....",
                    "encoded_file": "...."
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
                'import_log': <string: import_log>
            }
            """
        try:
            job_input = FilterModule._validate_job_ctx(job_ctx)
            self._logger.info("Job INPUT:\n" + str(job_input))

            encoded_file = job_input.get("encoded_file")
            file_format = job_input.get("file_format")
            decoded = base64.decodestring(encoded_file)

            cluster_id = job_ctx.get('contrail_cluster_id')
            cluster_token = job_ctx.get('auth_token')

            cc_host = job_input.get('contrail_command_host')
            cc_username = job_input.get('cc_username')
            cc_password = job_input.get('cc_password')

            self._logger.warn("Starting Server Import")
            cc_node_obj = CreateCCNode(cc_host, cluster_id, cluster_token,
                                       cc_username, cc_password)

            if file_format.lower() == "yaml":
                data = yaml.load(decoded)

            elif file_format.lower() == "json":
                data = self.convert(json.loads(decoded))
            else:
                raise ValueError('File format not recognized. Only yaml or '
                                 'json supported')
            added_nodes = self.import_nodes(
                data, cc_node_obj)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'import_log': ImportLog.instance().dump()
            }

        return {
            'status': 'success',
            'added_nodes': added_nodes,
            'import_log': ImportLog.instance().dump()
        }
