import sys
import argparse
import inspect

from cliff.app import App
from .contrailCli import ContrailCli
from .help import HelpAction
from .commandmanager import CommandManager
from contrail_cli_commandlist import commands_list
import contrail_cli_apps

class ContrailCliApp(App):

    def __init__(self, port):
        super(ContrailCliApp, self).__init__(
            description='ContrailCli Viewer',
            version='0.1',
            command_manager=CommandManager(self.NAME),
            deferred_help=True,
            )
	if self.NAME in commands_list:
	    for command in commands_list[self.NAME]:
	        self.command_manager.add_command(command[1].keys()[0], None)
	self.command_manager.del_command('help')
	self.port = port

    def run_subcommand(self, argv):
        try:
            subcommand = self.command_manager.find_command(argv)
        except ValueError as err:
            # If there was no exact match, try to find a possible match
            the_cmd = argv[0]
            matching_commands = self.get_matching_commands(' '.join(argv))
	    if len(matching_commands) > 1:
                self.stdout.write('Did you mean one of these?\n')
                for match in matching_commands:
                    self.stdout.write('  %s\n' % match)
                return 2
	    elif len(matching_commands) == 1:
		subcommand = self.command_manager.find_command(matching_commands[0].split(' '))
	    else:
		print "no matching command!"
		return 2
        cmd_factory, cmd_name, sub_argv = subcommand
        kwargs = {}
        if 'cmd_name' in inspect.getargspec(cmd_factory.__init__).args:
            kwargs['cmd_name'] = cmd_name
        cmd = cmd_factory(self, self.options, **kwargs)
        if not self.options.http_ip:
            self.options.http_ip = '127.0.0.1'
	if cmd_name != 'complete':
	    cmd.set_http_port(self.port, self.options.http_ip)
        err = None
        result = 1
        self.prepare_to_run_command(cmd)
        full_name = (cmd_name
                         if self.interactive_mode
                         else ' '.join([self.NAME, cmd_name])
                         )
        cmd_parser = cmd.get_parser(full_name)
        parsed_args = cmd_parser.parse_args(sub_argv)
        result = cmd.run(parsed_args)
        self.clean_up(cmd, result, None)
        return result

    def run(self, argv):
        """Equivalent to the main program for the application.

        :param argv: input arguments and options
        :paramtype argv: list of str
        """
	set_help = False
	if "-h" in argv or "--help" in argv:
	    set_help = True;
        self.options, remainder = self.parser.parse_known_args(argv)
	self.options.log_file = None
        self.interactive_mode = not remainder
        if self.deferred_help and self.options.deferred_help and remainder:
            self.options.deferred_help = False
        self.initialize_app(remainder)
        if self.deferred_help and self.options.deferred_help:
            action = HelpAction(None, None, default=self)
            action(self.parser, self.options, None, None)
        result = 1
        if not self.interactive_mode:
	    if set_help:
	        remainder.append("-h")
            result = self.run_subcommand(remainder)

    def get_matching_commands(self, cmd):
        """return matches of partial command
        """
	all_cmds = []
	if self.NAME in commands_list:
	    for command in commands_list[self.NAME]:
	        all_cmds.append(command[1].keys()[0])
        dist = []
        for candidate in sorted(all_cmds):
            if candidate.startswith(cmd):
                dist.append(candidate)
                continue
	return dist

    def clean_up(self, cmd, result, err):
        if err:
            self.LOG.debug('got an error: %s', err)

    def build_option_parser(self, description, version,
                            argparse_kwargs=None):
        argparse_kwargs = argparse_kwargs or {}
        parser = argparse.ArgumentParser(
            description=description,
            add_help=False,
            **argparse_kwargs
        )
        if self.deferred_help:
            parser.add_argument(
                '-h', '--help',
                dest='deferred_help',
                action='store_true',
                help="Show help message and exit.",
            )
        else:
            parser.add_argument(
                '-h', '--help',
                action=HelpAction,
                nargs=0,
                default=self,  
                help="Show this help message and exit.",
            )
        parser.add_argument(
            '--port',
            action='store',
            default=None,
            help='Specify an introspect port to query on.',
	    type=int,
        )
        parser.add_argument(
            '--http_ip',
            action='store',
            default=None,
            help='Specify ip address of destination.',
        )
	return parser

def main(argv=sys.argv[1:]):
    myapp = ContrailCliApp()
    return myapp.run(argv)

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
