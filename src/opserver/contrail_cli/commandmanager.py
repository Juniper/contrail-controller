#   Copyright 2012-2013 OpenStack Foundation
#
#   Licensed under the Apache License, Version 2.0 (the "License"); you may
#   not use this file except in compliance with the License. You may obtain
#   a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#   WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#   License for the specific language governing permissions and limitations
#   under the License.
#

"""Modify cliff.CommandManager"""

import logging
import pkg_resources

import cliff.commandmanager
from cliff.commandmanager import EntryPointWrapper

LOG = logging.getLogger(__name__)


class CommandManager(cliff.commandmanager.CommandManager):
    """Add additional functionality to cliff.CommandManager

    Load additional command groups after initialization
    Add _command_group() methods
    """

    def __init__(self, namespace, convert_underscores=True):
        self.group_list = []
        super(CommandManager, self).__init__(namespace, convert_underscores)

    def load_commands(self, namespace):
        """Load all the commands from an entrypoint"""
	#namespace = "cliff.introspect"
        for ep in pkg_resources.iter_entry_points(namespace):
            #print('found command %r', ep.name)
            cmd_name = (ep.name.replace('_', ' ')
                        if self.convert_underscores
                        else ep.name)
            self.commands[cmd_name] = ep
        self.group_list.append(namespace)

    def add_command_group(self, group=None):
        """Adds another group of command entrypoints"""
        if group:
            self.load_commands(group)

    def add_command(self, name, command_class):
	if command_class is not None:
            name = name.replace('_', ' ')
            self.commands[name] = EntryPointWrapper(name, command_class)
	    return
	namespace = "contrailCli"
        for ep in pkg_resources.iter_entry_points(namespace):
            #print('found command %r %r', ep.name, name)
	    if ep.name != name:
		continue
            cmd_name = (ep.name.replace('_', ' ')
                        if self.convert_underscores
                        else ep.name)
            self.commands[cmd_name] = ep
        #self.group_list.append(namespace)

    def del_command(self, name):
	if name in self.commands:
            del self.commands[name]

    def get_command_groups(self):
        """Returns a list of the loaded command groups"""
        return self.group_list

    def get_added_commands(self):
        command_list = []
        default_command_list = ['quit', 'exit', 'help']
        group_list = self.get_command_groups()
        for ns in group_list:
            for ep in pkg_resources.iter_entry_points(ns):
                cmd_name = (ep.name.replace('_', ' ')
                            if self.convert_underscores
                            else ep.name)
                command_list.append(cmd_name)
        command_list += default_command_list
        return command_list


