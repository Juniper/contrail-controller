import argparse
import sys
import traceback
import os

from cliff.command import Command


class HelpAction(argparse.Action):
    """Provide a custom action so the -h and --help options
    to the main app will print a list of the commands.

    The commands are determined by checking the CommandManager
    instance, passed in as the "default" value for the action.
    """
    def __call__(self, parser, namespace, commands, option_string=None):

        rows, columns = os.popen('stty size', 'r').read().split()
        app = self.default
        app.stdout.write('\nCommands:\n')
        command_manager = app.command_manager
        for name, ep in sorted(command_manager):
            if name == "complete":
                continue
            for command in commands:
                if name in command[1].keys():
                    one_liner = command[1].values()[0].keys()[0]
            if len(name) > 35:
                app.stdout.write('%s\n' % (name))
                app.stdout.write('                                 %s\n' % (one_liner))
            elif 35 + len(one_liner) > columns:
                app.stdout.write('%s\n' % (name))
                app.stdout.write('                                 %s\n' % (one_liner))
            else:
                app.stdout.write('  %-33s  %s\n' % (name, one_liner))
