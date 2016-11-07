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
    def __call__(self, parser, namespace, values, option_string=None):

        rows, columns = os.popen('stty size', 'r').read().split()
        app = self.default
        app.stdout.write('\nCommands:\n')
        command_manager = app.command_manager
        for name, ep in sorted(command_manager):
            try:
                factory = ep.load()
            except Exception as err:
                app.stdout.write('Could not load %r\n' % ep)
                if namespace.debug:
                    traceback.print_exc(file=app.stdout)
                continue
            try:
                cmd = factory(app, None)
                if cmd.deprecated:
                    continue
            except Exception as err:
                app.stdout.write('Could not instantiate %r: %s\n' % (ep, err))
                if namespace.debug:
                    traceback.print_exc(file=app.stdout)
                continue
            if name == "complete":
                one_liner = cmd.get_description().split('\n')[0]
            else:
                one_liner = cmd.get_description(name)
            if len(name) > 35:
                app.stdout.write('%s\n' % (name))
                app.stdout.write('                                 %s\n' % (one_liner))
            elif 35 + len(one_liner) > columns:
                app.stdout.write('%s\n' % (name))
                app.stdout.write('                                 %s\n' % (one_liner))
            else:
                app.stdout.write('  %-33s  %s\n' % (name, one_liner))
