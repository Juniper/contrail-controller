#!/usr/bin/env python

# vim: tabstop=4 shiftwidth=4 softtabstop=4
"""
   Name : interactive.py
   Author : Nitish Krishna
   Description : This in an implementation of the Cliff InteractiveApp Class
   This class allows intelligent auto-completion of options in the ciff shell command line
"""

import cmd2
import shlex
from cliff.interactive import InteractiveApp
import json
import ast
from os import listdir
from os import path
from os import getcwd


class IntrospectInteractiveApp(InteractiveApp):

    def __init__(self, port, parent_app, command_manager, stdin, stdout):
        InteractiveApp.__init__(
            self,
            parent_app=parent_app,
            command_manager=command_manager,
            stdin=stdin,
            stdout=stdout
        )
	self.port = port

    def print_topics(self, header, cmds, cmdlen, maxcol):
        if cmds:
            print header
            if self.ruler:
                print self.ruler * len(header)
            (cmds_per_line,junk)=divmod(maxcol,cmdlen)
            col=cmds_per_line
            for cmd in cmds:
                if col==0: print
                print (("%-"+`cmdlen`+"s") % cmd),
                col = (col+1) % cmds_per_line
            print "\n"
    #def default(self, line):
        # Tie in the default command processor to
        # dispatch commands known to the command manager.
        # We send the message through our parent app,
        # since it already has the logic for executing
        # the subcommand.
        #line_parts = shlex.split(line.parsed.raw)
	#print "in default {0}".format(line_parts)
	#line_parts.insert(1, str(self.port))
	#print "in default {0}".format(line_parts)
        #self.parent_app.run_subcommand(line_parts)

    def precmd(self, statement):
        # Pre-process the parsed command in case it looks like one of
        # our subcommands, since cmd2 does not handle multi-part
        # command names by default.
        line_parts = shlex.split(statement.parsed.raw)
	#print "in precmd{0}".format(statement.parsed.raw)
	#print line_parts
        try:
            the_cmd = self.command_manager.find_command(line_parts)
	    #print "tme_cmd is {0}".format(the_cmd)
            cmd_factory, cmd_name, sub_argv = the_cmd
	    sub_argv.insert(0, str(self.port))
	    statement.parsed.raw = statement.parsed.raw + ' ' + str(self.port)
	    #print "cmd_name is {0}, {1}".format(cmd_name, sub_argv)
        except ValueError:
            # Not a plugin command
            pass
        else:
            statement.parsed.command = str(cmd_name)
            statement.parsed.args = ' '.join(sub_argv)
	    #print "statement is {0}, {1}".format(statement.parsed.command, statement.parsed.args)
        return statement
    def auto_complete_file_list(self, last_arg=None):
	#print "in auto_complete_file_list"
        file_list = []
        dir_list = []
        if last_arg in ['-f', '--file_name']:
            last_arg = None
        if not last_arg:
            file_path = '.'
            file_list = [f for f in listdir(file_path) if path.isfile(f)]
            dir_list = [d for d in listdir(file_path) if path.isdir(d)]
        elif last_arg.startswith("/"):
            file_path_parts = last_arg.split("/")
            last_arg = file_path_parts[-1]
            file_path_parts = file_path_parts[1:-1]
            file_path = ""
            for part in file_path_parts:
                file_path += "/" + str(part)
            file_path += "/"
            file_list = [f for f in listdir(file_path) if path.isfile(str(file_path) + f)
                         and str(f).startswith(last_arg)]
            dir_list = [d for d in listdir(file_path) if path.isdir(str(file_path) + d)
                        and str(d).startswith(last_arg)]
            dir_list = [d + "/" if str(d) == last_arg else d for d in dir_list]
            file_list = [str(file_path) + f for f in file_list]
            dir_list = [str(file_path) + d for d in dir_list]
        else:
            file_path = getcwd()
            relative_path = ""
            if "/" in last_arg:
                file_path_parts = last_arg.split("/")
                last_arg = file_path_parts[-1]
                file_path_parts = file_path_parts[:-1]
                for part in file_path_parts:
                    file_path += "/" + str(part)
                    relative_path += str(part) + "/"
            file_path += "/"
            file_list = [f for f in listdir(file_path) if path.isfile(str(file_path) + f)
                         and str(f).startswith(last_arg)]
            dir_list = [d for d in listdir(file_path) if path.isdir(str(file_path) + d)
                        and str(d).startswith(last_arg)]
            dir_list = [d + "/" if str(d) == last_arg else d for d in dir_list]
            file_list = [str(relative_path) + f for f in file_list]
            dir_list = [str(relative_path) + d for d in dir_list]
        return file_list + dir_list

    def auto_complete_sub_option(self, chosen_sub_option, chosen_sub_command, line, last_arg=None):
	#print "in auto_complete_sub_option"
        obj = None
        files = None
        smgr_dict = self.parent_app.get_smgr_config()
        ip = smgr_dict["smgr_ip"]
        port = smgr_dict["smgr_port"]
        rest_api_params = None
        return_list = list()
        if chosen_sub_option in ['--server_id']:
            obj = 'server'
            rest_api_params = {
                'object': "server",
                'select': "id"
            }
        elif chosen_sub_option in ['--tag']:
            obj = 'tag'
            rest_api_params = {
                'object': "tag",
                'select': None
            }
        elif chosen_sub_option in ['--cluster_id']:
            obj = 'cluster'
            rest_api_params = {
                'object': "cluster",
                'select': None
            }
        elif chosen_sub_option in ['--image_id', '--package_image_id']:
            obj = 'image'
            rest_api_params = {
                'object': "image",
                'select': None
            }
        elif chosen_sub_option in ['--mac']:
            obj = 'mac'
            rest_api_params = {
                'object': "server",
                'select': "mac_address"
            }
        elif chosen_sub_option in ['--ip']:
            obj = 'ip'
            rest_api_params = {
                'object': "server",
                'select': "ip_address"
            }
        elif chosen_sub_option in ['-f', '--file_name']:
            files = self.auto_complete_file_list(last_arg)
        else:
            return []
        if obj:
            rest_api_params["match_key"] = None
            rest_api_params["match_value"] = None
            resp = smgrutils.send_REST_request(ip, port, rest_api_params=rest_api_params, method="GET")
            if resp:
                json_dict = ast.literal_eval(str(resp))
                auto_fill_list = smgrutils.convert_json_to_list(obj=obj, json_resp=json_dict)
                return_list = [
                    str(str(line).rsplit(' ', 1)[0] + " " + af_option)
                    for af_option in auto_fill_list
                    if str(str(line).rsplit(' ', 1)[0] + " " + af_option).startswith(line)
                ]
        elif files:
            return_list = [
                str(str(line).rsplit(' ', 1)[0] + " " + f)
                for f in files
                if str(str(line).rsplit(' ', 1)[0] + " " + f).startswith(line)
            ]
        return return_list

    def auto_complete_command(self, chosen_command, line, last_arg=None):
	#print "in auto_complete_command"
        obj = None
        files = None
        smgr_dict = self.parent_app.get_smgr_config()
        ip = smgr_dict["smgr_ip"]
        port = smgr_dict["smgr_port"]
        rest_api_params = None
        return_list = list()
        if chosen_command in ['reimage', 'provision']:
            obj = 'image'
            rest_api_params = {
                'object': "image",
                'select': "id,category"
            }
        else:
            files = self.auto_complete_file_list(last_arg)
        if obj:
            rest_api_params["match_key"] = None
            rest_api_params["match_value"] = None
            resp = smgrutils.send_REST_request(ip, port, rest_api_params=rest_api_params, detail=False, method="GET")
            if resp:
                json_dict = ast.literal_eval(resp)
                new_json_dict = dict()
                new_json_dict[obj] = list()
                for data_dict in json_dict[obj]:
                    new_dict = dict()
                    data_dict = dict(data_dict)
                    if (data_dict["category"] == "image" and chosen_command == "reimage") or \
                            (data_dict["category"] == "package" and chosen_command == "provision"):
                        new_dict["id"] = data_dict["id"]
                        new_json_dict[obj].append(new_dict)
                auto_fill_list = smgrutils.convert_json_to_list(obj=obj, json_resp=new_json_dict)
                return_list = [
                    str(str(line).rsplit(' ', 1)[0] + " " + af_option)
                    for af_option in auto_fill_list
                    if str(str(line).rsplit(' ', 1)[0] + " " + af_option).startswith(line)
                ]
        elif files:
            return_list = [
                str(str(line).rsplit(' ', 1)[0] + " " + f)
                for f in files
                if str(str(line).rsplit(' ', 1)[0] + " " + f).startswith(line)
            ]
        return return_list

    def _complete_prefix(self, prefix):
        """Returns cliff style commands with a specific prefix."""
	#print "in _complete_prefix {0} nikhil".format(prefix)
	#temp = [n for n, v in self.command_manager]
	#print "commands {0}".format(str(temp))
        if not prefix:
	    #for x in temp:
		#print "found {0}".format(x)
            return [n for n, v in self.command_manager]
        return [n for n, v in self.command_manager if n.startswith(prefix)]

    def completenames(self, text, *ignored):
	#print "in completenames"
        try:
            completions = cmd2.Cmd.completenames(self, text, *ignored)
            completions += self._complete_prefix(text)
            completions = [c for c in completions if c in self.command_manager.get_added_commands()]
            return completions
        except Exception as e:
            return str(e)

    #Overriding this class method with custom auto-completion
    def completedefault(self, text, line, begidx, endidx):
	#print "in completedefault {0} {1} {2} {3}".format(str(text), str(line), begidx, endidx)
        try:
            # This dictionary classes all the possbile SM options such that only one can be chosen at a time.

            # Splitting the command line arguments into: top_level_command (add, delete, display, etc),
            # chosen_sub_command (server, cluster, etc) and chosen_sub_option (--server_id, --tag, --detail, etc)
            available_options_list = list()
            chosen_sub_option_set = set()
            chosen_sub_option_list = list()
            line_args = str(line).split()

            top_level_matching_commands = [x[begidx:]
                                           for x in self._complete_prefix(line)
                                           if x in self.command_manager.get_added_commands()]
            # If line matches some top level command, return the said command or list of commands
            if len(top_level_matching_commands) > 0:
		#print "len >0"
                return top_level_matching_commands
            else:
		#print "len <0"
                chosen_command = [n for n, v in self.command_manager
                                  if (str(line).startswith(n) or str(line) == n)
                                  and n in self.command_manager.get_added_commands()][0]
		#print chosen_command
                cmd_factory, cmd_name, sub_argv = self.command_manager.find_command([str(chosen_command)])
                cmd = cmd_factory(self.parent_app, self.parent_app.options)
                cmd.get_parser(cmd_name)
		#print chosen_command
                cmd_dict = cmd.get_command_options()
		#print line
		#print str(cmd_dict)
                chosen_sub_command = None
                chosen_sub_option = None
                if str(chosen_command) in cmd_dict.keys():

                    line_args = str(line).split()
                    last_arg = line_args[-1]
                    if last_arg == chosen_command and line[-1] == " ":
                        last_arg = ""
                    if len(chosen_sub_option_list) > 0 and last_arg != chosen_sub_option_list[-1] and line[-1] == " ":
                        last_arg = ""
                    if chosen_command == "show":
                        cmd_mandatory_args_dict = cmd.get_mandatory_options()
                        available_mandatory_options = \
                            set(cmd_mandatory_args_dict[chosen_command]).difference(chosen_sub_option_set)
			#print available_mandatory_options
			#print "nikhil"
                        cmd_dict[chosen_command] += [x for x in available_mandatory_options]
			#print cmd_dict
                        available_options_list += [x for x in available_mandatory_options]

                    sub_option_list = [str(str(line).rsplit(' ', 1)[0] + " " + so)
                                       for so in cmd_dict[str(chosen_command)]
                                       if str(so).startswith(last_arg)
                                       and so in available_options_list]
                    if len(chosen_sub_option_list) and len(sub_option_list):
                        sub_option_list += self.auto_complete_command(chosen_command, line, last_arg)
                    # If line matches some sub_option return the said sub_option or list of sub_options
                    if len(sub_option_list) > 0:
                        return [x[begidx:] for x in sub_option_list]
                    chosen_sub_option = chosen_sub_option_list[-1]
                elif len(cmd_dict.keys()):

                    # Sub_command options exist
                    sub_command_list = [str(chosen_command + " " + op)
                                        for op in cmd_dict.keys()
                                        if str(chosen_command + " " + op).startswith(line)]
                    if len(sub_command_list):
                        return [x[begidx:] for x in sub_command_list]
                    chosen_sub_command = [sc for sc in cmd_dict.keys()
                                          if str(line).startswith(chosen_command + " " + sc)
                                          or str(line) == str(chosen_command + " " + sc)][0]

                    line_args = str(line).split()
                    last_arg = line_args[-1]
                    if last_arg == chosen_sub_command and line[-1] == " ":
                        last_arg = ""
                    if len(chosen_sub_option_list) > 0 and last_arg != chosen_sub_option_list[-1] and line[-1] == " ":
                        last_arg = ""

                    # For the add command, the sub option list includes the list of mandatory parameters for that ojbect
                    if chosen_command == "add" or chosen_command == "update":
                        cmd_mandatory_args_dict = cmd.get_mandatory_options()
                        available_mandatory_options = \
                            set(cmd_mandatory_args_dict[chosen_sub_command]).difference(chosen_sub_option_set)
                        cmd_dict[chosen_sub_command] += [x for x in available_mandatory_options]
                        available_options_list += [x for x in available_mandatory_options]
                        if chosen_sub_command == "tag" and '--tags' not in chosen_sub_option_set:
                            available_options_list += ['--tags']
                    sub_option_list = [str(str(line).rsplit(' ', 1)[0] + " " + so)
                                       for so in cmd_dict[str(chosen_sub_command)]
                                       if str(so).startswith(last_arg)
                                       and so in available_options_list]
                    if len(sub_option_list) > 0:
                        return [x[begidx:] for x in sub_option_list]
                    chosen_sub_option = chosen_sub_option_list[-1]
                else:
                    # Random command
                    return_list = list()
                    el = line[begidx:]
                    return_list.append(el)
                    return return_list
                # If some sub_option is chosen and the user presses TAB, the client tries to intelligently autocomplete
                # the statement using a REST call to Smgr to check if the sub_option has a correspoinding object
                return_list = list()
                if chosen_sub_option:
                    return_list = self.auto_complete_sub_option(chosen_sub_option, chosen_sub_command, line, last_arg)
                if len(return_list) > 0:
                    return [x[begidx:] for x in return_list]
        except Exception as e:
            return_list = list()
            el = line[begidx:]
            return_list.append(e)
            return return_list

    def cmdloop(self):
	#print "in cmdloop"
        try:
            self._cmdloop()
	    #print "after cmdloop"
        except Exception as e:
            self.parent_app.print_error_message_and_quit("\nException caught in interactive mode: " + str(e) + "\n")
