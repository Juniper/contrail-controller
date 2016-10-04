
import sys
import json
import subprocess
from lxml.html import etree
from lxml.html import parse
import lxml.html
from cliff.command import Command

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

    def my_print(self, data, level, is_base_val=False):
        if is_base_val is False:
            print ""
            for i in range(0, 2*level):
                print " ",
        print data,

    def web_invoke(self, httplink):
        cmd = 'curl --silent "' + httplink + '"'
        output = None
        try:
            output = subprocess.check_output(cmd, shell=True)
        except Exception:
            output = None
        return output
    # end web_invoke

    def print_struct(self, struct, level, is_last_element=False, print_name=False):
        if print_name == False:
            self.my_print("{", level)
        else:
	    if struct.tag == "list":
                self.my_print("\"{0}\" : ".format(struct.getparent().tag), level)
	    else:
                self.my_print("\"{0}\" : ".format(struct.tag), level)
            self.my_print("{", level, True)
        members = struct.getchildren()
        size = len(members)
        count = 0 
        for member in members:
            count = count + 1 
            last_element = False
            if count == size:
                last_element = True
            member_type = member.get("type")
            if member_type == "list":
                self.print_list(member.find("list"), level+1, last_element)
            elif member_type == "struct":
                member_children = member.getchildren()
                for member_child in member_children:
                    self.print_struct(member_child, level+1, last_element, True)
            elif member_type is None:
                continue
            elif member_type == "string":
                if last_element:
                    data = "\"{0}\" : \"{1}\"".format(member.tag, member.text)
                else:
                    data = "\"{0}\" : \"{1}\",".format(member.tag, member.text)
                self.my_print(data, level+1)
            else:
                if last_element:
                    data = "\"{0}\" : {1}".format(member.tag, member.text)
                else:
                    data = "\"{0}\" : {1},".format(member.tag, member.text)
                self.my_print(data, level+1)
        if is_last_element:
            self.my_print("}", level)
        else:
            self.my_print("},", level)
    #end print_struct

    def print_list(self, node, level, is_last_element=False):
        self.my_print("\"{0}\" : [".format(node.getparent().tag), level)
        typ = node.get("type")
        size = node.get("size")
        list_elements = node.getchildren()
        count = 0
        last_element = False
        for element in list_elements:
            count = count + 1
            if (count >= int(size)):
                last_element = True
            if typ == "struct":
                self.print_struct(element, level+1, last_element)
            elif typ == "list":
                self.print_list(element.find("list"), level+1, last_element)
            elif typ == "string":
                if last_element:
                    self.my_print("\"{0}\"".format(element.text), level+1)
                else:
                    self.my_print("\"{0}\",".format(element.text), level+1)
            else:
                if last_element:
                    self.my_print("\"{0}\"".format(element.text), level+1)
                else:
                    self.my_print("\"{0}\",".format(element.text), level+1)
        if is_last_element:
            self.my_print("]", level)
        else:
            self.my_print("],", level)
    # end print_list

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
        parser = etree.HTMLParser()
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
            	    tables = self.web_invoke(tab_url)
            	    root = etree.fromstring(tables)
            	    children = root.getchildren()
	    	    size = len(children)
	    	    count = 0
            	    for child in children:
	      	        count = count + 1
	      	        last_element = False
	                if count == size - 1:
		            last_element = True
              	        tag_type = child.get("type")
                        if tag_type == "list":
                            self.print_list(child.find("list"), 0, True)
                        elif tag_type == "struct":
                            member_children = child.getchildren()
                            for member_child in member_children:
                                self.print_struct(member_child, 0, last_element, True)
	                else:
		            print "{0}	: {1}".format(child.tag, child.text)

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

