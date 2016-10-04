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
        #parser.print_help(app.stdout)
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
            one_liner = cmd.get_description().split('\n')[0]
	    if len(name) > 35:
                app.stdout.write('%s\n' % (name))
                app.stdout.write('                                 %s\n' % (one_liner))
	    elif 35 + len(one_liner) > columns:
                app.stdout.write('%s\n' % (name))
                app.stdout.write('                                 %s\n' % (one_liner))
	    else:
                app.stdout.write('  %-33s  %s\n' % (name, one_liner))
        sys.exit(0)


class HelpCommand(Command):
    """print detailed help for another command
    """

    def get_parser(self, prog_name):
        parser = super(HelpCommand, self).get_parser(prog_name)
        parser.add_argument('cmd',
                            nargs='*',
                            help='name of the command',
                            )
        return parser

    def take_action(self, parsed_args):
	print "in HelpCommand"
        if parsed_args.cmd:
            try:
                the_cmd = self.app.command_manager.find_command(
                    parsed_args.cmd,
                )
                cmd_factory, cmd_name, search_args = the_cmd
            except ValueError:
                # Did not find an exact match
                cmd = parsed_args.cmd[0]
                fuzzy_matches = [k[0] for k in self.app.command_manager
                                 if k[0].startswith(cmd)
                                 ]
                if not fuzzy_matches:
                    raise
                self.app.stdout.write('Command "%s" matches:\n' % cmd)
                for fm in sorted(fuzzy_matches):
                    self.app.stdout.write('  %s\n' % fm)
                return
            self.app_args.cmd = search_args
            cmd = cmd_factory(self.app, self.app_args)
            full_name = (cmd_name
                         if self.app.interactive_mode
                         else ' '.join([self.app.NAME, cmd_name])
                         )
            cmd_parser = cmd.get_parser(full_name)
        else:
            action = HelpAction2(None, None, default=self.app)
            #action(self.app.parser, self.app.parser, None, None)
        #cmd_parser.print_help(self.app.stdout)
        return 0
