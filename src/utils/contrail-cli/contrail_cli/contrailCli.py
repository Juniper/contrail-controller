import sys
if 'threading' in sys.modules:
    del sys.modules['threading']
from opserver.opserver_util import OpServerUtils
import json
import subprocess
from cliff.command import Command
import xmltodict
import requests

class ContrailCli(Command):
    def __init__(self, app, app_args, cmd_name=None):
        super(ContrailCli, self).__init__(app, app_args)
        self.cmd_name = cmd_name
        self.cmd_list = []

    def web_invoke(self, url):
        output = None
        resp = requests.get(url, timeout=2)
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
            if cmd_name in command[1].keys():
                return command[1].values()[0].keys()[0]
        return
    #end get_description

    def get_parser(self, prog_name):
        self.prog_name = prog_name
        parser = super(ContrailCli, self).get_parser(prog_name)
        cmd_name = None
        if self.cmd_name:
            cmd_name = '_'.join(self.cmd_name.split(' '))
        cmd_name = self.cmd_name
        for command in self.cmd_list:
            if cmd_name in command[1].keys():
                for fields in command[1].values()[0].values():
                    for args in fields:
                        parser.add_argument("--"+args[0], nargs=1, help=args[1], metavar='\b')
        return parser

    def set_http_port(self, port, http_ip):
        self.http_port = str(port)
        self.http_ip = http_ip

    def set_cmd_list(self, cmd_list):
        self.cmd_list = cmd_list

    def take_action(self, parsed_args):
        ip_addr = self.http_ip
        http_port = self.http_port
        cmd_name = self.cmd_name
        for command in self.cmd_list:
            if cmd_name in command[1].keys():
                tab_url = "http://" + ip_addr + ":" +\
                       http_port + "/Snh_" + command[0].encode("utf-8")
                arg_count = 0
                if len(command[1].values()[0].values()):
                    tab_url = tab_url + "?"
                for params in command[1].values()[0].values():
                    for args in params:
                        args[0] = args[0].encode("utf-8")
                        if arg_count > 0:
                            tab_url = tab_url + "&"
                        tab_url = tab_url + args[0] + "="
                        if hasattr(parsed_args, args[0]) == True:
                            if getattr(parsed_args, args[0]) != None:
                                tab_url = tab_url + getattr(parsed_args, args[0])[0]
                        arg_count = arg_count + 1
                tables = self.web_invoke(tab_url)
                if tables:
                    doc = xmltodict.parse(tables)
                    OpServerUtils.messages_dict_scrub(doc)
                    print json.dumps(doc, indent=4)
