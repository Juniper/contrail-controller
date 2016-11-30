#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import argparse
import sys
from prettytable import PrettyTable
from prettytable import PLAIN_COLUMNS

from cliff.command import Command


class HelpAction(argparse.Action):
    """Provide a custom action so the -h and --help options
    to the main app will print a list of the commands.

    The commands are determined by checking the CommandManager
    instance, passed in as the "default" value for the action.
    """
    def __call__(self, parser, namespace, commands, option_string=None):

        app = self.default
        app.stdout.write('\nCommands:\n')
        command_manager = app.command_manager
        table = PrettyTable(["cli_name", "cli_help"])
        table.set_style(PLAIN_COLUMNS)
        table.border = False
        table.header = False
        table.align = "l"
        table.left_padding_width = 2
        for name, ep in sorted(command_manager):
            if name == "complete":
                continue
            for command in commands:
                if name == command.cli_name:
                    one_liner = command.cli_help
                    table.add_row([name, one_liner])
                    break
        app.stdout.write('%s\n' %table)
