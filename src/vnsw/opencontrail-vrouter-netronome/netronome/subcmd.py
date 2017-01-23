# vim: set expandtab shiftwidth=4 fileencoding=UTF-8:

# Copyright 2016 Netronome Systems, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import copy
import inspect
import logging
import os
import re
import sys

_PROG = os.path.split(sys.argv[0])[-1]
_LOGGER = logging.getLogger(_PROG)


def exception_msg(e, *args):
    """Create log message for exception during config."""

    msg = list(args)
    msg.append(type(e).__name__)
    e_str = str(e)
    if e_str:
        msg.append(e_str)
    return ': '.join(msg)


def format_getdoc(s):
    if s is None:
        return None

    s = re.sub(r'\s+', ' ', s.rstrip('.'), re.S).strip()

    try:
        return s[0].lower() + s[1:]
    except IndexError:
        return s


class Subcmd(object):
    """A subcommand."""

    def __init__(self, name, logger, description=None, features=(), **kwds):
        super(Subcmd, self).__init__(**kwds)

        self._description = description
        self.name = name
        self.logger = logger

        self._features = list(features)

    def add_feature(self, feature):
        self._features.append(feature)

    def apply_features(self, fn, *args, **kwds):
        for f in self._features:
            if hasattr(f, fn):
                getattr(f, fn)(self, *args, **kwds)

    def parse_args(self, prog, command, args):
        raise NotImplementedError(
            '{}.parse_args()'.format(type(self).__name__)
        )

    def run(self):
        raise NotImplementedError('{}.run()'.format(type(self).__name__))

    @property
    def description(self):
        return self._description \
            or format_getdoc(inspect.getdoc(self)) \
            or type(self).__name__


def _known_cmds_help(cmds):
    if not cmds:
        return ''

    msg = ['known commands:']
    for c in cmds:
        msg.append('    {:<15} {}'.format(c.name, c.description))

    return ''.join(map(lambda s: s + '\n', msg))


def _make_HelpFormatter(cmds):
    class _HelpFormatter(argparse.ArgumentDefaultsHelpFormatter):
        def format_usage(self):
            ans = super(_HelpFormatter, self).format_usage()
            return ''.join((ans, _known_cmds_help(cmds)))

        def format_help(self):
            ans = super(_HelpFormatter, self).format_help()
            #ans = re.sub('<command>', '<command> [<args>]', ans)
            return '\n'.join((ans, _known_cmds_help(cmds)))

    return _HelpFormatter


class SubcmdApp(object):
    """
    A CLI with named subcommands, each of which can have different options.
    """

    def __init__(self, cmds=None, prog=None, logger=None, **kwds):
        super(SubcmdApp, self).__init__(**kwds)

        self.cmds = [] if cmds is None else cmds
        self.prog = _PROG if prog is None else prog
        self.logger = _LOGGER if logger is None else logger

        assert hasattr(self, 'cmd_map')

    @property
    def cmds(self):
        return self._cmds

    @cmds.setter
    def cmds(self, value):
        try:
            cmd_map = {x.name: x for x in value}
        except (AttributeError, TypeError):
            raise ValueError('invalid value for "cmds"')

        self._cmds = list(value)
        self.cmd_map = cmd_map

    def run(self, argv=None):
        argv = sys.argv if argv is None else argv

        parser = argparse.ArgumentParser(
            prog=self.prog,
            formatter_class=_make_HelpFormatter(self.cmds),
        )
        parser.add_argument(
            'command', metavar='<command>', help='Subcommand to run',
        )

        # Leave logging alone if unit tests (or other outside code) have
        # already configured it.
        if not logging.root.handlers:
            h = logging.StreamHandler()
            h.setFormatter(logging.Formatter(logging.BASIC_FORMAT, None))
            logging.root.addHandler(h)
        else:
            h = None

        args, tail = parser.parse_known_args(argv[1:])
        if args.command not in self.cmd_map:
            self.logger.error('unknown command "%s"', args.command)
            return 2

        try:
            cmd = self.cmd_map[args.command]
            cmd.parse_args(
                prog=self.prog,
                command=args.command,
                args=tail
            )

            if h is not None:
                logging.root.removeHandler(h)

            cmd.apply_features('run')
            rc = cmd.run()

            assert isinstance(rc, int), \
                '{}.run() did not return an int'.format(type(cmd).__name__)
            return rc

        except (AttributeError, KeyError, NameError, TypeError) as e:
            # Selected "unexpected" exceptions.
            logging.basicConfig(level=logging.DEBUG)  # fallback
            self.logger.exception(exception_msg(e))
            return 1

        except SystemExit as e:
            # oslo_config raises SystemExit rather than a normal exception when
            # command-line parsing fails. In the case of an invalid
            # command-line argument value (parser raises ValueError),
            # oslo_config raises SystemExit without any arguments. This sets
            # e.code to None which means "success." (oslo_config bug.)
            #
            # Work around this by returning 2 (command-line parsing error) if
            # e.code is None. Otherwise Nova will not signal an error if it
            # passes us bad command-line data (a la LP1584625).
            return 2 if e.code is None else e.code

        except ImportError as e:
            logging.basicConfig(level=logging.DEBUG)  # fallback
            self.logger.critical(exception_msg(e))
            return 1

        except BaseException as e:
            logging.basicConfig(level=logging.DEBUG)  # fallback
            self.logger.error(exception_msg(e))
            return 1
