#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import sys
if 'threading' in sys.modules:
    del sys.modules['threading']
import json
from cliff.command import Command
import xmltodict
import requests
from urllib3.exceptions import InsecureRequestWarning
import warnings
warnings.simplefilter('ignore', InsecureRequestWarning)

class ContrailCli(Command):
    def __init__(self, app, app_args, cmd_name=None):
        super(ContrailCli, self).__init__(app, app_args)
        self.cmd_name = cmd_name
        self.cmd_list = []

    def web_invoke(self, url):
        output = None
        try:
            http_url = "http" + url
            resp = requests.get(http_url, timeout=2)
        except requests.ConnectionError:
            http_url = "https" + url
            resp = requests.get(http_url, timeout=2, verify=False)
        if resp.status_code == requests.codes.ok:
            output = resp.text
        return output
    # end web_invoke

    def get_description(self, cmd_name=None):
        """Return the command description.
        """
        if cmd_name == None:
            return
        for command in self.cmd_list:
            if cmd_name == command.cli_name:
                return command.cli_help
        return
    #end get_description

    def _add_fields_to_parser(self, parser, command):
        for fields in command.cli_params:
            parser.add_argument("--"+fields.param_name, nargs=1, help=fields.param_help, metavar='\b')
    #end _add_fields_to_parser

    def get_parser(self, prog_name):
        self.prog_name = prog_name
        parser = super(ContrailCli, self).get_parser(prog_name)
        cmd_name = self.cmd_name
        for command in self.cmd_list:
            if cmd_name == command.cli_name:
                self._add_fields_to_parser(parser, command)
        return parser

    def set_http_port(self, port, http_ip):
        self.http_port = str(port)
        self.http_ip = http_ip

    def set_cmd_list(self, cmd_list):
        self.cmd_list = cmd_list

    def _prepare_query_url(self, parsed_args, command):
        ip_addr = self.http_ip
        http_port = self.http_port
        url = "://" + ip_addr + ":" + http_port + "/Snh_" + command.struct_name
        arg_count = 0
        for params in command.cli_params:
            if arg_count > 0:
                url = url + "&"
            else:
                url = url + "?"
            url = url + params.param_name + "="
            if hasattr(parsed_args, params.param_name) == True:
                if getattr(parsed_args, params.param_name) != None:
                    url = url + getattr(parsed_args, params.param_name)[0]
            arg_count = arg_count + 1
        return url
    #end _prepare_query_url

    def _json_loads_check(self, value):
        try:
            json_value = json.loads(value)
        except:
            return False, None
        else:
            return True, json_value
    # end _json_loads_check

    def _messages_dict_remove_keys(self, messages_dict, key_pattern):
        for key, value in messages_dict.items():
            if key_pattern in key:
                del messages_dict[key]
            if isinstance(value, list):
                for elem in value:
                    if isinstance(elem, dict):
                        self._messages_dict_remove_keys(
                            elem, key_pattern)
            if isinstance(value, dict):
                self._messages_dict_remove_keys(value, key_pattern)
    # end _messages_dict_remove_keys

    def _messages_dict_flatten_key(self, messages_dict, key_match):
        for key, value in messages_dict.items():
            if isinstance(value, dict):
                if key_match in value:
                    messages_dict[key] = value[key_match]
                else:
                    self._messages_dict_flatten_key(value, key_match)
            if isinstance(value, list):
                for elem in value:
                    if isinstance(elem, dict):
                        self._messages_dict_flatten_key(elem,
                            key_match)
    #end _messages_dict_flatten_key

    def _messages_dict_eval(self, messages_dict):
        for key, value in messages_dict.iteritems():
            if isinstance(value, basestring):
                # try json.loads
                success, json_value = self._json_loads_check(value)
                if success:
                    messages_dict[key] = json_value
                    continue
            if isinstance(value, dict):
                self._messages_dict_eval(value)
            if isinstance(value, list):
                for elem in value:
                    if isinstance(elem, dict):
                        self._messages_dict_eval(elem)
    # end _messages_dict_eval

    def messages_dict_scrub(self, messages_dict):
        self._messages_dict_remove_keys(messages_dict, '@')
        self._messages_dict_flatten_key(messages_dict, '#text')
        self._messages_dict_eval(messages_dict)
    # end messages_dict_scrub

    def take_action(self, parsed_args):
        cmd_name = self.cmd_name
        for command in self.cmd_list:
            if cmd_name == command.cli_name:
                url = self._prepare_query_url(parsed_args, command)
                result = self.web_invoke(url)
                if result:
                    output = xmltodict.parse(result)
                    self.messages_dict_scrub(output)
                    print json.dumps(output, indent=4)
