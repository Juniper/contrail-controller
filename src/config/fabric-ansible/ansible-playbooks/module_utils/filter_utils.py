import logging
import os
from functools import wraps

class FilterLog(object):
    _instance = None

    @staticmethod
    def instance(loggername=None):
        if not FilterLog._instance:
            FilterLog._instance = FilterLog(loggername)
        return FilterLog._instance
    # end instance

    @staticmethod
    def cleanup_filterlog_instance():
        if FilterLog._instance:
            FilterLog._instance = None
    # end cleanup_filterlog_instance

    @staticmethod
    def _init_logging(loggername):
        """
        :return: type=<logging.Logger>
        """
        logger = logging.getLogger(loggername)
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.INFO)

        formatter = logging.Formatter(
            '%(asctime)s %(levelname)-8s %(message)s',
            datefmt='%Y/%m/%d %H:%M:%S'
        )
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)
        return logger
      # end _init_logging

    def __init__(self, loggername):
        self._msg = None
        self._logs = []
        self._logger = FilterLog._init_logging(loggername)
    # end __init__

    def logger(self):
        return self._logger
    # end logger

    def msg_append(self, msg):
        if msg:
            if not self._msg:
                self._msg = msg + ' ... '
            else:
                self._msg += msg + ' ... '
    # end log

    def msg_end(self):
        if self._msg:
            self._msg += 'done'
            self._logs.append(self._msg)
            self._logger.info(self._msg)
            self._msg = None
    # end msg_end

    def msg_error(self, msg):
        self._logger.error(msg)
    # end msg_error

    def msg_debug(self, msg):
        self._logger.debug(msg)
    # end msg_debug

    def msg_warn(self, msg):
        self._logger.warn(msg)
    # end msg_warn

    def dump(self):
        retval = ""
        for msg in self._logs:
            retval += msg + '\n'
        return retval
    # end dump
# end FilterLog


def _task_log(msg):
    FilterLog.instance().msg_append(msg)
# end _task_log


def _task_done(msg=None):
    if msg:
        _task_log(msg)
    FilterLog.instance().msg_end()
# end _task_done


def _task_error_log(msg):
    FilterLog.instance().msg_error(msg)
# end _task_error_log


def _task_debug_log(msg):
    FilterLog.instance().msg_debug(msg)
# end _task_debug_log


def _task_warn_log(msg):
    FilterLog.instance().msg_warn(msg)
# end _task_warn_log

def validate_payload(func):
    @wraps(func)
    def decorated(self, *args, **kwargs):
        # pre function call validations
        validator_method = args[-1]
        validator_method(args[1])
        _task_log("Validated parsed payload")
        # post function call validations
        return func(self, *args, **kwargs)
    return decorated


