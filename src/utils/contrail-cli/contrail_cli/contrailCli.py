#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import sys
if 'threading' in sys.modules:
    del sys.modules['threading']
from opserver.opserver_util import OpServerUtils
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

    def take_action(self, parsed_args):
        cmd_name = self.cmd_name
        for command in self.cmd_list:
            if cmd_name == command.cli_name:
                url = self._prepare_query_url(parsed_args, command)
                result = self.web_invoke(url)
                if result:
                    output = xmltodict.parse(result)
                    OpServerUtils.messages_dict_scrub(output)
                    print json.dumps(output, indent=4)
