import sys
if 'threading' in sys.modules:
    del sys.modules['threading']
from opserver.opserver_util import OpServerUtils
import json
import subprocess
from cliff.command import Command
import xmltodict

class ContrailCli(Command):
    command_dictionary = {}
    mandatory_params_args = {}

    def __init__(self, app, app_args, cmd_name=None):
	super(ContrailCli, self).__init__(app, app_args)
	self.cmd_name = cmd_name

    def get_command_options(self):
        return self.command_dictionary

    def get_mandatory_options(self):                                               
 	return self.mandatory_params_args

    def web_invoke(self, httplink):
        cmd = 'curl --silent "' + httplink + '"'
        output = None
        try:
            output = subprocess.check_output(cmd, shell=True)
        except Exception:
            output = None
        return output
    # end web_invoke

    def get_parser(self, prog_name):
	self.prog_name = prog_name
        parser = super(ContrailCli, self).get_parser(prog_name)
	self.command_dictionary["show"] = []
	self.mandatory_params_args["show"] = []
	cli_name = prog_name.split(' ')[0]
	cmd_name = None
	if self.cmd_name:
	    cmd_name = '_'.join(self.cmd_name.split(' '))
	cmd_name = self.cmd_name
	if cli_name in commands_list:
	  for command in commands_list[cli_name]:
	    if cmd_name in command[1].keys():
	        for fields in command[1].values()[0].values():
	            for args in fields:
			#print args[1]
        	        parser.add_argument("--"+args[0], nargs=1, help=args[1], default='0', metavar='\b')
        return parser

    def set_http_port(self, port):
	self.http_port = str(port)

    def take_action(self, parsed_args): 
        ip_addr = '127.0.0.1'
        http_port = self.http_port
	cmd_name = self.cmd_name
	cli_name = self.prog_name.split(' ')[0]
	if cli_name in commands_list:
	    for command in commands_list[cli_name]:
	        if self.cmd_name in command[1].keys():
                    tab_url = "http://" + ip_addr + ":" +\
                       http_port + "/Snh_" + command[0].encode("utf-8")
	    	    arg_count = 0
		    if len(command[1].values()[0].values()):
			tab_url = tab_url + "?"
	    	    for params in command[1].values()[0].values():
	      	        for args in params: 
		    	    args[0] = args[0].encode("utf-8")
			    if arg_count == 0:
				print getattr(parsed_args,args[0])
		                tab_url = tab_url + args[0] + "=" + getattr(parsed_args, args[0])[0]
			    else:
		    	        tab_url = tab_url + "&" + args[0] + "=" + getattr(parsed_args, args[0])[0]
			    arg_count = arg_count + 1
            	    xml_data = self.web_invoke(tab_url)
		    data = xmltodict.parse(xml_data)
		    OpServerUtils.messages_dict_scrub(data)
		    print json.dumps(data, indent=4)

class ContrailCli_DbQueueParamsSet(ContrailCli):
	"""
	sandesh request to set Database Queue params
	"""

class ContrailCli_SmQueueParamsSet(ContrailCli):
	"""
	sandesh request to set State Machine Queue params
	"""


class ContrailCli_SmQueueParamsStatus(ContrailCli):
	"""
	sandesh request to get State Machine Queue params
	"""


class ContrailCli_DbQueueParamsStatus(ContrailCli):
	"""
	sandesh request to get Database Queue params
	"""


class ContrailCli_SmQueueParamsReset(ContrailCli):
	"""
	sandesh request to reset State Machine Queue params
	"""

class ContrailCli_RedisUVERequest(ContrailCli):
	"""
	Sandesh Request message for Redis data
	"""

commands_list = {'contrail-collector-cli': [(u'RedisUVERequest', {u'show redis data': {u'Sandesh Request message for Redis data': []}}), (u'SmQueueParamsStatus', {u'show smqueue params': {u'sandesh request to get State Machine Queue params': []}}), (u'DbQueueParamsSet', {u'set dbqueue params': {u'sandesh request to set Database Queue params': [[u'high', u'is it high watermark'], [u'queue_count', u'what is queue count'], [u'drop_level', u'what is drop_level']]}}), (u'DbQueueParamsStatus', {u'show dbqueue params': {u'sandesh request to get Database Queue params': []}}), (u'SmQueueParamsSet', {u'set smqueue params': {u'sandesh request to set State Machine Queue params': [[u'high', u''], [u'queue_count', u''], [u'drop_level', u'']]}}), (u'SmQueueParamsReset', {u'reset dbqueue params': {u'sandesh request to reset State Machine Queue params': []}})]}

