# sqlalchemy_/log.py
# Copyright (C) 2006-2016 the SQLAlchemy authors and contributors
# <see AUTHORS file>
# Includes alterations by Vinay Sajip vinay_sajip@yahoo.co.uk
#
# This module is part of SQLAlchemy and is released under
# the MIT License: http://www.opensource.org/licenses/mit-license.php

"""Logging control and utilities.

Control of logging for SA can be performed from the regular python logging
module.  The regular dotted module namespace is used, starting at
'sqlalchemy_'.  For class-level logging, the class name is appended.

The "echo" keyword parameter, available on SQLA :class:`.Engine`
and :class:`.Pool` objects, corresponds to a logger specific to that
instance only.

"""

import logging
import sys

# set initial level to WARN.  This so that
# log statements don't occur in the absence of explicit
# logging being enabled for 'sqlalchemy_'.
rootlogger = logging.getLogger('sqlalchemy_')
if rootlogger.level == logging.NOTSET:
    rootlogger.setLevel(logging.WARN)


def _add_default_handler(logger):
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter(
        '%(asctime)s %(levelname)s %(name)s %(message)s'))
    logger.addHandler(handler)


_logged_classes = set()


def class_logger(cls):
    logger = logging.getLogger(cls.__module__ + "." + cls.__name__)
    cls._should_log_debug = lambda self: logger.isEnabledFor(logging.DEBUG)
    cls._should_log_info = lambda self: logger.isEnabledFor(logging.INFO)
    cls.logger = logger
    _logged_classes.add(cls)
    return cls


class Identified(object):
    logging_name = None

    def _should_log_debug(self):
        return self.logger.isEnabledFor(logging.DEBUG)

    def _should_log_info(self):
        return self.logger.isEnabledFor(logging.INFO)


class InstanceLogger(object):
    """A logger adapter (wrapper) for :class:`.Identified` subclasses.

    This allows multiple instances (e.g. Engine or Pool instances)
    to share a logger, but have its verbosity controlled on a
    per-instance basis.

    The basic functionality is to return a logging level
    which is based on an instance's echo setting.

    Default implementation is:

    'debug' -> logging.DEBUG
    True    -> logging.INFO
    False   -> Effective level of underlying logger
               (logging.WARNING by default)
    None    -> same as False
    """

    # Map echo settings to logger levels
    _echo_map = {
        None: logging.NOTSET,
        False: logging.NOTSET,
        True: logging.INFO,
        'debug': logging.DEBUG,
    }

    def __init__(self, echo, name):
        self.echo = echo
        self.logger = logging.getLogger(name)

        # if echo flag is enabled and no handlers,
        # add a handler to the list
        if self._echo_map[echo] <= logging.INFO \
           and not self.logger.handlers:
            _add_default_handler(self.logger)

    #
    # Boilerplate convenience methods
    #
    def debug(self, msg, *args, **kwargs):
        """Delegate a debug call to the underlying logger."""

        self.log(logging.DEBUG, msg, *args, **kwargs)

    def info(self, msg, *args, **kwargs):
        """Delegate an info call to the underlying logger."""

        self.log(logging.INFO, msg, *args, **kwargs)

    def warning(self, msg, *args, **kwargs):
        """Delegate a warning call to the underlying logger."""

        self.log(logging.WARNING, msg, *args, **kwargs)

    warn = warning

    def error(self, msg, *args, **kwargs):
        """
        Delegate an error call to the underlying logger.
        """
        self.log(logging.ERROR, msg, *args, **kwargs)

    def exception(self, msg, *args, **kwargs):
        """Delegate an exception call to the underlying logger."""

        kwargs["exc_info"] = 1
        self.log(logging.ERROR, msg, *args, **kwargs)

    def critical(self, msg, *args, **kwargs):
        """Delegate a critical call to the underlying logger."""

        self.log(logging.CRITICAL, msg, *args, **kwargs)

    def log(self, level, msg, *args, **kwargs):
        """Delegate a log call to the underlying logger.

        The level here is determined by the echo
        flag as well as that of the underlying logger, and
        logger._log() is called directly.

        """

        # inline the logic from isEnabledFor(),
        # getEffectiveLevel(), to avoid overhead.

        if self.logger.manager.disable >= level:
            return

        selected_level = self._echo_map[self.echo]
        if selected_level == logging.NOTSET:
            selected_level = self.logger.getEffectiveLevel()

        if level >= selected_level:
            self.logger._log(level, msg, args, **kwargs)

    def isEnabledFor(self, level):
        """Is this logger enabled for level 'level'?"""

        if self.logger.manager.disable >= level:
            return False
        return level >= self.getEffectiveLevel()

    def getEffectiveLevel(self):
        """What's the effective level for this logger?"""

        level = self._echo_map[self.echo]
        if level == logging.NOTSET:
            level = self.logger.getEffectiveLevel()
        return level


def instance_logger(instance, echoflag=None):
    """create a logger for an instance that implements :class:`.Identified`."""

    if instance.logging_name:
        name = "%s.%s.%s" % (instance.__class__.__module__,
                             instance.__class__.__name__,
                             instance.logging_name)
    else:
        name = "%s.%s" % (instance.__class__.__module__,
                          instance.__class__.__name__)

    instance._echo = echoflag

    if echoflag in (False, None):
        # if no echo setting or False, return a Logger directly,
        # avoiding overhead of filtering
        logger = logging.getLogger(name)
    else:
        # if a specified echo flag, return an EchoLogger,
        # which checks the flag, overrides normal log
        # levels by calling logger._log()
        logger = InstanceLogger(echoflag, name)

    instance.logger = logger


class echo_property(object):
    __doc__ = """\
    When ``True``, enable log output for this element.

    This has the effect of setting the Python logging level for the namespace
    of this element's class and object reference.  A value of boolean ``True``
    indicates that the loglevel ``logging.INFO`` will be set for the logger,
    whereas the string value ``debug`` will set the loglevel to
    ``logging.DEBUG``.
    """

    def __get__(self, instance, owner):
        if instance is None:
            return self
        else:
            return instance._echo

    def __set__(self, instance, value):
        instance_logger(instance, echoflag=value)
