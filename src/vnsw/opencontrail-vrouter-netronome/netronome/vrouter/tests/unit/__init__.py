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

from __future__ import absolute_import

import contextlib
import json
import logging
import re

from netronome.vrouter import pci

__all__ = (
    'LogMessageCounter',
    'SetLogLevel',
    'attachLogHandler',
    'make_vfset',
    'make_getLogger',
    'setLogLevel',
    'explain_assertion',
)


class LogMessageCounter(logging.Handler):
    """
    Counts log messages (and also captures them, for debugging).
    """

    def __init__(self, **kwds):
        logging.Handler.__init__(self)
        self.count = {}
        self.logs = []
        self.levelnames = set()

    def emit(self, record):
        if record.name not in self.count:
            c = self.count[record.name] = {}
        else:
            c = self.count[record.name]

        if record.levelname not in c:
            c[record.levelname] = 1
        else:
            c[record.levelname] += 1

        self.logs.append((record.name, record.levelname, record.getMessage()))
        self.levelnames.add(record.levelname)

    def __str__(self):
        return json.dumps(self.logs, indent=4)


class SetLogLevel(object):
    """
    Sets the log level for a set of loggers, typically at file scope.

    This is originally intended for urllib3 and/or "requests" library logging.
    The DEBUG and INFO messages for these loggers are radically different
    between different releases of OpenStack Kilo and OpenStack Mitaka (the
    loggers have different names, etc). We have no interest in checking these.

    We do leave warnings, errors, etc. enabled however, as we aren't expecting
    any of these.
    """

    def __init__(self, loggers, level, **kwds):
        super(SetLogLevel, self).__init__(**kwds)
        self.loggers = loggers
        self.level = level
        self.old = {}

    def setUp(self):
        assert not self.old, 'setUp called multiple times'
        for k in self.loggers:
            logger = logging.getLogger(k)
            self.old[k] = logger.level
            logger.setLevel(self.level)

    def tearDown(self):
        old, self.old = self.old, {}
        for k, v in old.iteritems():
            logging.getLogger(k).setLevel(v)


@contextlib.contextmanager
def attachLogHandler(logger, handler=None, level=logging.DEBUG):
    if handler is None:
        handler = logging.NullHandler()

    logger.addHandler(handler)
    if level is not None:
        old = logger.level
        logger.setLevel(level)

    yield handler

    if level is not None:
        logger.setLevel(old)
    logger.removeHandler(handler)


@contextlib.contextmanager
def setLogLevel(logger, level=logging.DEBUG):
    old = logger.level
    logger.setLevel(level)

    yield

    logger.setLevel(old)


def make_vfset(str):
    return frozenset([
        pci.parse_pci_address(a) for a in re.split(r'\s+', str.strip())
    ])


def make_getLogger(name, cls=LogMessageCounter):
    def fn():
        return logging.getLogger(name)

    def lmc(cls=cls):
        return attachLogHandler(fn(), cls())

    fn.name = name
    fn.lmc = lmc
    return fn


def explain_assertion(args, msg, delim='\n\n'):
    return (args[0] + delim + msg,) + args[1:]


class LogHandlerError(Exception):
    pass


class ForbiddenLogHandler(logging.Handler):
    def emit(self, record):
        import sys
        import tempfile
        import traceback

        t = tempfile.NamedTemporaryFile(delete=False)
        frames = sys._current_frames()
        for k, f in frames.iteritems():
            print >>t, '~~~ {} ~~~'.format(k)
            traceback.print_stack(f=f, file=t)
            print >>t

        raise LogHandlerError(
            'logging to {} is forbidden; traceback in {}'.format(
                record.name, t.name
            )
        )


def _forbid(name):
    logger = logging.getLogger(name)
    logger.addHandler(ForbiddenLogHandler())
    # logger.setLevel(logging.DEBUG)

# _forbid('sqlalchemy.pool.NullPool')
