#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Contains common logger initialization.

This is to be used in ansible internals as well as ansible modules
"""

from builtins import str
import logging
from logging.handlers import RotatingFileHandler
import os

from ansible import constants as CONST

BASE_LOG_PATH = '/var/log/contrail/'
DEFAULT_ANSIBLE_LOG_PATH = BASE_LOG_PATH + \
    'config-device-manager/contrail-fabric-ansible-playbooks.log'
LOGGING_FORMAT = \
    '%(asctime)s.%(msecs)03d %(name)s [%(levelname)s]:  %(message)s'
DATE_FORMAT = "%m/%d/%Y %H:%M:%S"
MAX_BYTES = 5000000
BACKUP_COUNT = 10

# Context attribute along with it's abbeviation for logging
# Include attributes in this list if you want them to appear in log
# message header
extra_list = {
    'job_execution_id': 'eid',
    # add more entries here...
}


def fabric_ansible_logger(name, ctx=None):
    name = name
    ctx = ctx
    debug = CONST.DEFAULT_DEBUG
    verbosity = CONST.DEFAULT_VERBOSITY
    logfile = CONST.DEFAULT_LOG_PATH or DEFAULT_ANSIBLE_LOG_PATH
    # Log more if ANSIBLE_DEBUG or -v[v] is set.
    if debug is True:
        level = logging.DEBUG
    elif verbosity == 1:
        level = logging.INFO
    elif verbosity > 1:
        level = logging.DEBUG
    else:
        level = logging.INFO
    # If log file is writable, init normal logger, otherwise use null logger
    # to avoid logging errors
    if (logfile is not None and os.path.exists(logfile) and os.access(
            logfile, os.W_OK)) or os.access(os.path.dirname(logfile), os.W_OK):
        name_hdr = "[{}]".format(name)
        name_hdr += " pid={}".format(str(os.getpid()))
        # Include any context attributes
        if ctx:
            for k, v in list(extra_list.items()):
                if k in ctx:
                    name_hdr += " {}={}".format(v, ctx[k])
        logger = logging.getLogger(name_hdr)
        logger.setLevel(level)
        logger.propagate = False

        if not len(logger.handlers):
            logging_file_handler = RotatingFileHandler(
                filename=logfile, maxBytes=MAX_BYTES, backupCount=BACKUP_COUNT)
            log_format = logging.Formatter(LOGGING_FORMAT, datefmt=DATE_FORMAT)
            logging_file_handler.setFormatter(log_format)
            logger.addHandler(logging_file_handler)
    else:
        raise Exception("Cannot write to log file at {}".format(logfile))

    return logger

