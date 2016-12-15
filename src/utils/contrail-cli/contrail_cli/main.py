#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import inspect

from cliff.app import App
import logging
logging.basicConfig()
from .contrailCli import ContrailCli
from .help import HelpAction
from .commandmanager import CommandManager
from sandesh_common.vns.constants import ServiceHttpPortMap
from collections import namedtuple
cli_mapping = namedtuple("cli_mapping", ("struct_name", "cli_name",
    "cli_help", "cli_params"))
cli_params = namedtuple("cli_params", ("param_name", "param_help"))

class ContrailCliApp(App):

    def __init__(self, commands_list):
        super(ContrailCliApp, self).__init__(
            description='ContrailCli Viewer',
            version='0.1',
            command_manager=CommandManager(self.NAME),
            deferred_help=True,
            )
        self.cmd_list = []
        for command in commands_list:
            cps_list = []
            for fields in command[1].values()[0].values():
                for args in fields:
                    cps = cli_params(param_name=args[0], param_help=args[1])
                    cps_list.append(cps)
            scm = cli_mapping(struct_name=command[0],
                    cli_name=command[1].keys()[0],
                    cli_help=command[1].values()[0].keys()[0],
                    cli_params=cps_list)
            self.cmd_list.append(scm)
        for command in self.cmd_list:
            self.command_manager.add_command(command.cli_name, None)
        self.command_manager.del_command('help')
        self.port = None
        self.mod_name = self.NAME
        cli_pos = self.NAME.find("-cli")
        if cli_pos > 0:
            self.mod_name = self.NAME[:cli_pos]
            if self.mod_name in ServiceHttpPortMap:
                self.port = ServiceHttpPortMap[self.mod_name]
        if self.port == None:
            print "No port specified, exiting"
            exit(-1)

    def run_subcommand(self, argv):
        try:
            subcommand = self.command_manager.find_command(argv)
        except ValueError as err:
            # If there was no exact match, try to find possible match(es)
            the_cmd = argv[0]
            matching_commands = self.get_matching_commands(' '.join(argv))
            if len(matching_commands) > 1:
                self.stdout.write('Did you mean one of these?\n')
                for match in matching_commands:
                    self.stdout.write('  %s\n' % match)
                return
            elif len(matching_commands) == 1:
                subcommand = self.command_manager.find_command(matching_commands[0].split(' '))
            else:
                print "no matching command!"
                return
        cmd_factory, cmd_name, sub_argv = subcommand
        kwargs = {}
        if 'cmd_name' in inspect.getargspec(cmd_factory.__init__).args:
            kwargs['cmd_name'] = cmd_name
        cmd = cmd_factory(self, self.options, **kwargs)
        if cmd_name != 'complete':
            cmd.set_http_port(self.port if self.options.http_server_port is
                    None else self.options.http_server_port, self.options.http_server_ip)
            cmd.set_cmd_list(self.cmd_list)
        self.prepare_to_run_command(cmd)
        full_name = (cmd_name
                         if self.interactive_mode
                         else ' '.join([self.NAME, cmd_name])
                         )
        cmd_parser = cmd.get_parser(full_name)
        parsed_args = cmd_parser.parse_args(sub_argv)
        result = cmd.run(parsed_args)
        self.clean_up(cmd, result, None)
        return

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
        if not remainder and set_help == False:
            print "interactive mode not supported, exiting"
            return
        if self.deferred_help and self.options.deferred_help and remainder:
            self.options.deferred_help = False
        self.initialize_app(remainder)
        if self.deferred_help and self.options.deferred_help:
            action = HelpAction(None, None, default=self)
            action(self.parser, self.options, self.cmd_list, None)
            return
        if set_help:
            remainder.append("-h")
        self.run_subcommand(remainder)

    def get_matching_commands(self, cmd):
        """return matches of partial command
        """
        all_cmds = []
        for command in self.cmd_list:
            all_cmds.append(command.cli_name)
        dist = []
        for candidate in sorted(all_cmds):
            if candidate.startswith(cmd):
                dist.append(candidate)
                continue
        return dist

    def clean_up(self, cmd, result, err):
        if result:
            self.LOG.error('got an error: %s while executing cmd %s, \
                    exception %s', result, cmd.cmd_name, err)

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
            '--http_server_port',
            action='store',
            default=None,
            help='Specify an introspect port to query on.',
            type=int,
        )
        parser.add_argument(
            '--http_server_ip',
            action='store',
            default='127.0.0.1',
            help='Specify ip address of destination.',
        )
        return parser

def main(argv=sys.argv[1:]):
    cliapp = ContrailCliApp([])
    return cliapp.run(argv)

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
